import { batch, createMemo, createSignal } from 'solid-js';
import { createStore, reconcile } from 'solid-js/store';
import { dominantKind, resolveKind } from './damage-colors';
import {
  clearMarkerValues,
  flushMarkerValues,
  setMarkerValues,
} from './slot-elements';
import type {
  ActorStats,
  DamageEvent,
  DamageUpdate,
  NpcHealth,
  NpcNames,
  NpcSlots,
  NpcSlotsHide,
  NpcSlotsShow,
  StyleUpdate,
  TargetDamage,
} from './types';

/** Maximum amount of hits kept in the live feed. */
const MAX_FEED_ENTRIES = 12;

/** Age after which a hit is removed from the live feed (milliseconds). */
const FEED_TTL_MS = 5000;

/** How often the feed is checked for expired entries (milliseconds). */
const FEED_SWEEP_MS = 500;

/**
 * How often the size of the viewport is reported to the plugin (milliseconds).
 *
 * The report is repeated until the plugin answers with a frame of the
 * positions, so a report that was lost does not cost anything.
 */
const VIEWPORT_REPORT_INTERVAL_MS = 1000;

/**
 * Maximum amount of targets kept in the damage map.
 *
 * The map holds one entry per damaged target. The oldest entries are dropped
 * once the limit is reached, so a long session cannot grow it without bounds.
 */
const MAX_DAMAGE_TARGETS = 64;

/**
 * The number of the slots of the overlay that are drawn until the plugin
 * tells the real number.
 *
 * The plugin owns the slots, so it announces how many of them it has in its
 * styles. The number is only a fallback for the frames before that message
 * arrives.
 */
const DEFAULT_SLOT_COUNT = 50;

const [feed, setFeed] = createSignal<DamageEvent[]>([]);
const [actors, setActors] = createSignal<ActorStats[]>([]);
const [connected, setConnected] = createSignal(false);

/**
 * The NPCs of the slots that are occupied, keyed by the number of the slot.
 *
 * The plugin owns the slots: it hands a free one to every NPC that enters the
 * screen and takes it back when the NPC leaves. The id of an NPC only travels
 * when it enters the screen, so it is kept in a store of its own, keyed by
 * the number of the slot.
 */
const [npcSlots, setNpcSlots] = createStore<NpcSlots>({});

/**
 * The names of the NPCs, keyed by their form id.
 *
 * The names travel on a channel of their own, so they are kept in a store of
 * their own: a name never changes, so it is only sent when the view does not
 * know it yet.
 */
const [npcNames, setNpcNames] = createStore<NpcNames>({});

/**
 * The health of the NPCs, keyed by their form id.
 *
 * The plugin sends the health of an NPC when it enters the screen and every
 * time it takes damage, so the store holds the health of every NPC that is
 * on the screen. The bar above the head of an NPC looks its health up by
 * the id of the NPC.
 */
const [healthByNpc, setHealthByNpc] = createStore<NpcHealth>({});

/**
 * The number of the slots of the overlay.
 *
 * The plugin announces it together with the styles of the overlay.
 */
const [slotCount, setSlotCount] = createSignal(DEFAULT_SLOT_COUNT);

/**
 * The numbers of the slots of the overlay.
 *
 * The slots are drawn once and are never re-created: the plugin only fills
 * them. An NPC that enters or leaves the screen therefore never touches the
 * elements of the other ones.
 */
const slotIndexes = createMemo(() =>
  Array.from({ length: slotCount() }, (_, index) => index),
);

/**
 * The damage every target received, keyed by the form id of the target.
 *
 * The ids are the ones the plugin sends as the `targetId` of a damage event
 * and as the `id` of the NPC of a slot, so the overlay can look the damage of
 * a slot up directly.
 *
 * The damage lives in a store instead of a signal, so a hit only touches the
 * entry of its target and only the number above that head is drawn again.
 */
const [damageByTarget, setDamageByTarget] = createStore<
  Record<string, TargetDamage>
>({});

/**
 * The ids of the targets in the order they were hit, the oldest one first.
 *
 * The order decides which target is dropped when the damage grows past its
 * budget: the one that was hit the longest time ago.
 */
let damageOrder: string[] = [];

export {
  feed,
  actors,
  connected,
  npcSlots,
  npcNames,
  healthByNpc,
  slotCount,
  slotIndexes,
  damageByTarget,
};

const parseUpdate = (payload: unknown): DamageUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as DamageUpdate;
    if (!parsed || typeof parsed !== 'object') {
      return null;
    }
    return parsed;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

/**
 * Adds the damage of a single event to the damage of its target.
 *
 * The amount of a target is the sum of all the hits it took, so the number
 * grows with every hit. The kind of the most recent hit is kept as well, it
 * colours the number.
 */
const addDamage = (a_event: DamageEvent) => {
  const id = a_event.targetId;
  const current = damageByTarget[id];

  const entry: TargetDamage = {
    amount: (current?.amount ?? 0) + a_event.damage,
    kind: resolveKind(a_event.kind),
  };

  // Only the entry of the target is written, so the numbers above the other
  // heads are not drawn again.
  setDamageByTarget(id, entry);

  // The target moves to the end of the order, so the oldest one is the one
  // that was hit the longest time ago.
  const index = damageOrder.indexOf(id);

  if (index >= 0) {
    damageOrder.splice(index, 1);
  }

  damageOrder.push(id);

  // The oldest targets are dropped once the damage grows past its budget.
  while (damageOrder.length > MAX_DAMAGE_TARGETS) {
    const oldest = damageOrder.shift();

    if (oldest === undefined) {
      break;
    }

    const next: Record<string, TargetDamage> = {};

    for (const target of Object.keys(damageByTarget)) {
      if (target !== oldest) {
        const entry = damageByTarget[target];

        next[target] = {
          amount: entry.amount,
          kind: entry.kind,
        };
      }
    }

    setDamageByTarget(reconcile(next));
  }
};

/**
 * Builds the damage out of a complete state.
 *
 * The aggregates of the plugin hold the damage every actor took over the
 * whole session, so they are the base of the damage. The hits of the actors that
 * are not part of the aggregates are added from the events.
 */
const buildDamage = (
  a_actors: ActorStats[],
  a_events: DamageEvent[],
): Record<string, TargetDamage> => {
  const map: Record<string, TargetDamage> = {};

  for (const actor of a_actors) {
    if (actor.damageTaken <= 0) {
      continue;
    }

    map[actor.id] = {
      amount: actor.damageTaken,
      kind: dominantKind(actor.takenByKind),
    };
  }

  // The hits of an actor that is part of the aggregates are already counted.
  const aggregated = new Set(Object.keys(map));

  for (const event of a_events) {
    if (aggregated.has(event.targetId)) {
      continue;
    }

    const current = map[event.targetId];
    map[event.targetId] = {
      amount: (current?.amount ?? 0) + event.damage,
      kind: resolveKind(event.kind),
    };
  }

  return map;
};

const applyUpdate = (payload: string) => {
  const update = parseUpdate(payload);
  if (!update) {
    return;
  }

  setConnected(true);
  setActors(Array.isArray(update.actors) ? update.actors : []);

  if (update.type === 'snapshot') {
    setFeed(
      Array.isArray(update.events)
        ? update.events.slice(0, MAX_FEED_ENTRIES)
        : [],
    );

    // A snapshot carries the complete state, so the damage is rebuilt
    // instead of being extended.
    const damage = buildDamage(
      Array.isArray(update.actors) ? update.actors : [],
      Array.isArray(update.events) ? update.events : [],
    );

    setDamageByTarget(reconcile(damage));
    damageOrder = Object.keys(damage);

    return;
  }

  const event = update.event;
  if (event) {
    setFeed((previous) => [event, ...previous].slice(0, MAX_FEED_ENTRIES));
    addDamage(event);
  }
};

/**
 * Removes hits that are older than the TTL. The plugin sends wall clock
 * timestamps, so the age can be computed on this side.
 */
const sweepFeed = () => {
  const now = Date.now();

  setFeed((previous) => {
    const alive = previous.filter((entry) => now - entry.time <= FEED_TTL_MS);
    return alive.length === previous.length ? previous : alive;
  });
};

const parseNpcSlotsHide = (payload: unknown): NpcSlotsHide | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  const slots: NpcSlotsHide = [];

  // A frame is a plain list of the numbers of the slots, separated by
  // semicolons.
  for (const part of payload.split(';')) {
    if (!part) {
      continue;
    }

    const slot = Number(part);
    if (!Number.isInteger(slot) || slot < 0) {
      continue;
    }

    slots.push(slot);
  }

  return slots;
};

const parseNpcSlotsShow = (payload: unknown): NpcSlotsShow | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  const shown: NpcSlotsShow = [];

  // A frame is a list of the tuples "slot:id", separated by semicolons. The
  // id is a number in its hexadecimal notation, so it never contains a
  // separator.
  for (const tuple of payload.split(';')) {
    if (!tuple) {
      continue;
    }

    const separator = tuple.indexOf(':');
    if (separator <= 0) {
      continue;
    }

    const slot = Number(tuple.slice(0, separator));
    if (!Number.isInteger(slot) || slot < 0) {
      continue;
    }

    const id = tuple.slice(separator + 1);
    if (id.length === 0) {
      continue;
    }

    shown.push({ slot, id });
  }

  return shown;
};

const parseNpcNames = (payload: unknown): NpcNames | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as unknown;

    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      return null;
    }

    const names: NpcNames = {};

    for (const [id, name] of Object.entries(
      parsed as Record<string, unknown>,
    )) {
      if (id && typeof name === 'string') {
        names[id] = name;
      }
    }

    return names;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

const parseNpcHealth = (payload: unknown): NpcHealth | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as unknown;

    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      return null;
    }

    const health: NpcHealth = {};

    for (const [id, value] of Object.entries(
      parsed as Record<string, unknown>,
    )) {
      if (!id || !value || typeof value !== 'object') {
        continue;
      }

      const entry = value as { current?: unknown; max?: unknown };

      const current = Number(entry.current);
      const max = Number(entry.max);

      // A health without a maximum cannot be drawn as a bar.
      if (!Number.isFinite(current) || !Number.isFinite(max) || max <= 0) {
        continue;
      }

      health[id] = { current, max };
    }

    return health;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

const parseStyleUpdate = (payload: unknown): StyleUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as StyleUpdate;
    if (!parsed || typeof parsed !== 'object' || parsed.type !== 'style') {
      return null;
    }

    if (
      typeof parsed.baseFontSize !== 'number' ||
      !Number.isFinite(parsed.baseFontSize) ||
      parsed.baseFontSize <= 0
    ) {
      return null;
    }

    return parsed;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

/**
 * Applies the NPCs that entered the screen.
 *
 * The plugin sends the id of an NPC only once, when it enters the screen. It
 * is kept in a store of its own, keyed by the number of the slot, so the slot
 * can look it up without the plugin sending it again.
 *
 * The payload is a frame of the tuples "slot:id", separated by semicolons.
 */
const applyNpcSlotsShow = (payload: string) => {
  const shown = parseNpcSlotsShow(payload);
  if (!shown) {
    return;
  }

  batch(() => {
    for (const entry of shown) {
      // The id is only written for its own slot, so the slots of the other
      // ones are not drawn again.
      setNpcSlots(entry.slot, entry.id);
    }
  });
};

/**
 * Applies the names of the NPCs that entered the screen.
 *
 * The plugin sends the names on their own channel and only for the NPCs that
 * the view does not know yet, so the names of the NPCs that are already on
 * the screen are never sent twice.
 */
const applyNpcNames = (payload: string) => {
  const names = parseNpcNames(payload);
  if (!names) {
    return;
  }

  batch(() => {
    for (const [id, name] of Object.entries(names)) {
      // The name is only written for its own NPC, so the markers of the
      // other ones are not drawn again.
      setNpcNames(id, name);
    }
  });
};

/**
 * Applies the health of the NPCs.
 *
 * The health is keyed by the form id of the NPC, so the bar of a slot can
 * look it up by the id of the NPC that occupies the slot. The plugin sends
 * the health of an NPC when it enters the screen and every time it takes
 * damage.
 */
const applyNpcHealth = (payload: string) => {
  const health = parseNpcHealth(payload);
  if (!health) {
    return;
  }

  batch(() => {
    for (const [id, entry] of Object.entries(health)) {
      // The health is only written for its own NPC, so the bars of the
      // other ones are not drawn again.
      setHealthByNpc(id, entry);
    }
  });
};

/**
 * Drops the names of the NPCs of the old world.
 */
const applyNpcNamesClear = () => {
  setNpcNames(reconcile({}));
};

/**
 * Applies the NPCs that left the screen.
 *
 * The plugin sends the numbers of the slots that have to be freed, so the
 * store of the view only loses the entries of these slots.
 */
const applyNpcSlotsHide = (payload: string) => {
  const slots = parseNpcSlotsHide(payload);
  if (!slots) {
    return;
  }

  const next: NpcSlots = { ...npcSlots };
  const gone: string[] = [];

  for (const slot of slots) {
    const id = next[slot];

    if (id !== undefined) {
      gone.push(id);
    }

    delete next[slot];

    // The values of the slot are dropped, so an NPC that comes back to the
    // screen starts without a position again.
    clearMarkerValues(slot);
  }

  setNpcSlots(reconcile(next));

  // The health of an NPC that left the screen is dropped as well, so the
  // store does not grow over the session. The plugin sends the health again
  // when the NPC comes back.
  if (gone.length > 0) {
    const health: NpcHealth = { ...healthByNpc };

    for (const id of gone) {
      delete health[id];
    }

    setHealthByNpc(reconcile(health));
  }
};

/**
 * Applies the numbers above the heads of the NPCs that are visible on the
 * screen.
 *
 * A frame is a list of the tuples "slot:x,y,scale", separated by semicolons.
 * The slot is the number of the slot of the overlay, the numbers are the
 * plain CSS values of the number: the horizontal offset, the vertical offset
 * in whole pixels and the scale of the font.
 *
 * The values are written into the element of the slot, so only the slots that
 * really moved change and only those are drawn again.
 *
 * The frame is read with `indexOf` and `slice` in a single pass, so the parts
 * of the frame are never copied into arrays of their own.
 *
 * The frames of the plugin are joined when they arrive faster than the
 * browser draws them, so a slot can be in the payload more than once. The
 * values of a slot are kept by the slots themselves, so only the newest values
 * of a slot are written into its element.
 */
const applyNpcSlotsMove = (payload: string) => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return;
  }

  const length = payload.length;
  let start = 0;

  while (start < length) {
    // The end of the tuple is the next semicolon.
    let end = payload.indexOf(';', start);

    if (end === -1) {
      end = length;
    }

    if (end > start) {
      const separator = payload.indexOf(':', start);

      if (separator > start && separator < end) {
        const slot = Number(payload.slice(start, separator));

        if (Number.isInteger(slot) && slot >= 0) {
          const x = payload.indexOf(',', separator + 1);

          if (x > separator && x < end) {
            const y = payload.indexOf(',', x + 1);

            if (y > x && y < end) {
              // A tuple may carry more than three values, the rest is ignored.
              let scaleEnd = payload.indexOf(',', y + 1);

              if (scaleEnd === -1 || scaleEnd > end) {
                scaleEnd = end;
              }

              setMarkerValues(
                slot,
                payload.slice(separator + 1, x),
                payload.slice(x + 1, y),
                payload.slice(y + 1, scaleEnd),
              );
            }
          }
        }
      }
    }

    start = end + 1;
  }

  // The elements are written once, after the whole frame is read.
  flushMarkerValues();
};

/**
 * The frame of the positions that has not been applied yet.
 *
 * The plugin sends the positions of the slots that moved, so several frames
 * can arrive before the browser draws the next one. The frames that arrive in
 * the meantime are joined, because the frame of the plugin only carries the
 * slots that really moved: a dropped frame would leave a marker at the
 * position it had before.
 */
let pendingNpcSlotsMove: string | undefined;

/**
 * The handle of the animation frame that applies the collected positions.
 */
let npcSlotsMoveFrame: number | undefined;

const flushNpcSlotsMove = () => {
  npcSlotsMoveFrame = undefined;

  const payload = pendingNpcSlotsMove;
  pendingNpcSlotsMove = undefined;

  if (payload === undefined) {
    return;
  }

  applyNpcSlotsMove(payload);
};

/**
 * Collects the positions of the plugin and applies them once per frame.
 *
 * The plugin can send the positions faster than the browser draws them, so
 * the positions are not applied right away. They are applied in the next
 * animation frame, so the work is done at most once per frame.
 */
const scheduleNpcSlotsMove = (payload: string) => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return;
  }

  // The frames are joined in the order they arrived, so the newest position
  // of a slot is the one that is left when it is written.
  pendingNpcSlotsMove =
    pendingNpcSlotsMove === undefined
      ? payload
      : `${pendingNpcSlotsMove};${payload}`;

  if (npcSlotsMoveFrame !== undefined) {
    return;
  }

  npcSlotsMoveFrame = window.requestAnimationFrame(flushNpcSlotsMove);
};

/**
 * Applies the styles of the overlay.
 *
 * The plugin owns the styles of the numbers above the heads, so the base size
 * of their font is a constant of the plugin. The view only puts it into a
 * global custom property of the document, where the stylesheet picks it up.
 */
const applyStyleUpdate = (payload: string) => {
  const update = parseStyleUpdate(payload);

  if (!update) {
    return;
  }

  document.documentElement.style.setProperty(
    BASE_FONT_SIZE_PROPERTY,
    String(update.baseFontSize),
  );

  // The plugin owns the slots, so it announces how many of them the view has
  // to draw.
  if (
    typeof update.slotCount === 'number' &&
    Number.isInteger(update.slotCount) &&
    update.slotCount > 0
  ) {
    setSlotCount(update.slotCount);
  }

  // A frame proves that the plugin knows the size of the viewport, so
  // the report of the size is not needed anymore.
  stopViewportReport();
};

declare global {
  interface Window {
    /** Called by the plugin for every damage update. */
    onDamageUpdate?: (payload: string) => void;

    /** Called by the plugin for the NPCs that left the screen. */
    onNpcSlotsHide?: (payload: string) => void;

    /** Called by the plugin for the NPCs that entered the screen. */
    onNpcSlotsShow?: (payload: string) => void;

    /** Called by the plugin for the slots that moved. */
    onNpcSlotsMove?: (payload: string) => void;

    /** Called by the plugin for the names of the NPCs. */
    onNpcNames?: (payload: string) => void;
    onNpcNamesClear?: (payload: string) => void;

    /** Called by the plugin for the health of the NPCs. */
    onNpcHealth?: (payload: string) => void;

    /** Called by the plugin for the styles of the overlay. */
    onStyleUpdate?: (payload: string) => void;

    /** Called by the view to tell the plugin the size of the viewport. */
    onViewportUpdate?: (payload: string) => void;

    /** Called by the view to tell the plugin that it is ready to receive data. */
    onViewReady?: () => void;
  }
}

/**
 * The custom property that carries the base size of the font of the numbers.
 *
 * The plugin sends the value, so the stylesheet does not have to know it.
 */
const BASE_FONT_SIZE_PROPERTY = '--npc-damage-base-font-size';

/**
 * Reports the size of the viewport to the plugin.
 *
 * The plugin sends the positions of the markers in pixels, so it has to know
 * the size of the viewport. The size is reported when the view starts and
 * whenever it changes.
 */
const sendViewport = () => {
  const width = Math.round(window.innerWidth);
  const height = Math.round(window.innerHeight);

  if (width <= 0 || height <= 0) {
    return;
  }

  window.onViewportUpdate?.(`${width},${height}`);
};

/**
 * The timer that repeats the report of the size of the viewport.
 *
 * PrismaUI binds the function of the listener when the page finished
 * loading, so the first report can be lost. The report is repeated until the
 * plugin answers with a frame of the positions, which proves that it knows
 * the size of the viewport.
 */
let viewportTimer: number | undefined;

const stopViewportReport = () => {
  if (viewportTimer === undefined) {
    return;
  }

  window.clearInterval(viewportTimer);
  viewportTimer = undefined;
};

/**
 * Registers the bridge between the plugin and the view.
 *
 * The plugin calls `window.onDamageUpdate` as soon as its view becomes ready,
 * which can happen before this script is evaluated. To close that window the
 * view announces itself through `window.onViewReady`, which makes the plugin
 * push the complete state.
 */
export const initPluginOnWindow = () => {
  window.onDamageUpdate = applyUpdate;
  window.onNpcSlotsHide = applyNpcSlotsHide;
  window.onNpcSlotsShow = applyNpcSlotsShow;
  window.onNpcSlotsMove = scheduleNpcSlotsMove;
  window.onNpcNames = applyNpcNames;
  window.onNpcNamesClear = applyNpcNamesClear;
  window.onNpcHealth = applyNpcHealth;
  window.onStyleUpdate = applyStyleUpdate;

  window.setInterval(sweepFeed, FEED_SWEEP_MS);

  // The plugin turns the positions of the markers into pixels, so it has to
  // know the size of the viewport whenever it changes.
  window.addEventListener('resize', sendViewport);

  sendViewport();
  viewportTimer = window.setInterval(sendViewport, VIEWPORT_REPORT_INTERVAL_MS);

  const announce = () => {
    // The size of the viewport is reported first: the plugin cannot place a
    // marker without it.
    sendViewport();
    window.onViewReady?.();
  };

  // The first call may race with the plugin registering its listener, so the
  // announcement is repeated a few times.
  window.setTimeout(announce, 0);
  window.setTimeout(announce, 250);
  window.setTimeout(announce, 1000);
};
