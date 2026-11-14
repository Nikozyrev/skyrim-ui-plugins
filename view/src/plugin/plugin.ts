import { batch, createSignal } from 'solid-js';
import { createStore, reconcile } from 'solid-js/store';
import { dominantKind, resolveKind } from './damage-colors';
import { markerProperty } from './marker-properties';
import type {
  ActorStats,
  DamageEvent,
  DamageUpdate,
  NpcIdsUpdate,
  NpcNamesUpdate,
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

const [feed, setFeed] = createSignal<DamageEvent[]>([]);
const [actors, setActors] = createSignal<ActorStats[]>([]);
const [connected, setConnected] = createSignal(false);

/**
 * The ids of the NPCs that are currently visible on the screen.
 *
 * The ids are the stable key of a marker: the plugin only sends them when an
 * NPC entered or left the screen, so the view can reuse the elements it
 * already drew while the NPCs only move around.
 */
const [npcMarkerIds, setNpcMarkerIds] = createSignal<string[]>([]);

/**
 * The ids of the visible NPCs in the order of the frames.
 *
 * The frames address their markers by the index of the NPC in this list, so
 * the list is kept next to the properties of the markers.
 */
let npcOrder: string[] = [];

/**
 * The names of the NPCs that are visible on the screen, keyed by their id.
 *
 * The plugin sends the names of the NPCs that entered the screen in their own
 * message, so they are kept in a store of their own: the name of an NPC is
 * looked up by its id and never travels with the markers.
 */
const [npcNames, setNpcNames] = createStore<Record<string, string>>({});

/**
 * The damage every target received, keyed by the form id of the target.
 *
 * The ids are the ones the plugin sends as the `targetId` of a damage event
 * and as the `id` of an NPC marker, so the overlay can look the damage of a
 * marker up directly.
 *
 * The damage lives in a store instead of a signal, so a hit only touches the
 * entry of its target and only the number above that head is drawn again.
 */
const [damageByTarget, setDamageByTarget] = createStore<Record<string, TargetDamage>>({});

/**
 * The ids of the targets in the order they were hit, the oldest one first.
 *
 * The order decides which target is dropped when the damage grows past its
 * budget: the one that was hit the longest time ago.
 */
let damageOrder: string[] = [];

export { feed, actors, connected, npcMarkerIds, npcNames, damageByTarget };

/**
 * Writes the values of a NPC into the global custom properties of the
 * document.
 *
 * The properties are addressed by the id of the NPC, so a slot of the overlay
 * can point at them with the id of the NPC it shows. The stylesheet resolves
 * the reference on its own, so a frame does not have to touch the elements of
 * the NPCs that did not move.
 */
const setMarkerValues = (a_id: string, a_x: number, a_y: number, a_scale: number) => {
  const style = document.documentElement.style;

  style.setProperty(markerProperty(a_id, 'x'), String(a_x));
  style.setProperty(markerProperty(a_id, 'y'), String(a_y));
  style.setProperty(markerProperty(a_id, 'scale'), String(a_scale));

  // The marker is only drawn once its position is known.
  style.setProperty(markerProperty(a_id, 'placed'), '1');
};

/**
 * Drops the values of a NPC.
 *
 * The properties are removed instead of being reset, so a NPC that comes back
 * to the screen starts without a position again.
 */
const clearMarkerValues = (a_id: string) => {
  const style = document.documentElement.style;

  style.removeProperty(markerProperty(a_id, 'x'));
  style.removeProperty(markerProperty(a_id, 'y'));
  style.removeProperty(markerProperty(a_id, 'scale'));
  style.removeProperty(markerProperty(a_id, 'placed'));
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
const buildDamage = (a_actors: ActorStats[], a_events: DamageEvent[]): Record<string, TargetDamage> => {
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
    setFeed(Array.isArray(update.events) ? update.events.slice(0, MAX_FEED_ENTRIES) : []);

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

const parseNpcIdsUpdate = (payload: unknown): NpcIdsUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as NpcIdsUpdate;
    if (!parsed || typeof parsed !== 'object' || parsed.type !== 'npcIds') {
      return null;
    }
    return parsed;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

const parseNpcNamesUpdate = (payload: unknown): NpcNamesUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as NpcNamesUpdate;
    if (!parsed || typeof parsed !== 'object' || parsed.type !== 'npcNames') {
      return null;
    }
    return parsed;
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
 * Applies the names of the NPCs that entered the screen.
 *
 * The plugin sends the names in their own message and only for the NPCs that
 * the view does not know yet, so the names of the visible NPCs are kept in a
 * store of their own, keyed by the id of their NPC.
 */
const applyNpcNamesUpdate = (payload: string) => {
  const update = parseNpcNamesUpdate(payload);
  if (!update) {
    return;
  }

  if (!update.names || typeof update.names !== 'object') {
    return;
  }

  batch(() => {
    for (const [id, name] of Object.entries(update.names)) {
      if (!id || typeof name !== 'string') {
        continue;
      }

      // The name is only written for its own NPC, so the markers of the other
      // ones are not drawn again.
      setNpcNames(id, name);
    }
  });
};

const applyNpcNamesClear = () => {
  setNpcNames(reconcile({}));
}

/**
 * Applies the ids of the NPCs that are currently visible on the screen.
 *
 * The plugin only sends the ids when an NPC entered or left the screen, so
 * the list is replaced with the complete set of the visible NPCs. The order
 * of the list matters: the frames of the positions address their markers by
 * the index of the NPC in it.
 *
 * The names of the NPCs that left the screen are kept: they are only dropped
 * when the last NPC left the screen.
 */
const applyNpcIdsUpdate = (payload: string) => {
  const update = parseNpcIdsUpdate(payload);
  if (!update) {
    return;
  }

  const ids = Array.isArray(update.ids)
    ? update.ids.filter((id): id is string => typeof id === 'string' && id.length > 0)
    : [];

  const previous = new Set(npcOrder);
  const current = new Set(ids);

  npcOrder = ids;
  setNpcMarkerIds(ids);

  // The values of the NPCs that left the screen are dropped, so a NPC that
  // comes back to the screen starts without a position again.
  for (const id of previous) {
    if (!current.has(id)) {
      clearMarkerValues(id);
    }
  }
};

/**
 * Applies the numbers above the heads of the NPCs that are visible on the
 * screen.
 *
 * A frame is a list of the tuples "index:x,y,scale", separated by semicolons.
 * The index is the position of the NPC in the list of the ids, the numbers are
 * the plain CSS values of the number: the horizontal offset, the vertical
 * offset in whole pixels and the scale of the font.
 *
 * The values are written into the global custom properties of the NPC, so
 * only the markers that really moved change and only those are drawn again.
 */
const applyNpcMarkersUpdate = (payload: string) => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return;
  }

  let applied = false;

  for (const chunk of payload.split(';')) {
    if (!chunk) {
      continue;
    }

    const separator = chunk.indexOf(':');
    if (separator <= 0) {
      continue;
    }

    const id = npcOrder[Number(chunk.slice(0, separator))];
    if (!id) {
      continue;
    }

    const values = chunk.slice(separator + 1).split(',');
    if (values.length < 3) {
      continue;
    }

    const x = Number(values[0]);
    const y = Number(values[1]);
    const scale = Number(values[2]);

    if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(scale)) {
      continue;
    }

    setMarkerValues(id, x, y, scale);

    applied = true;
  }

  if (applied) {
    // A frame proves that the plugin knows the size of the viewport, so
    // the report of the size is not needed anymore.
    stopViewportReport();
  }
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

  document.documentElement.style.setProperty(BASE_FONT_SIZE_PROPERTY, String(update.baseFontSize));
};

declare global {
  interface Window {
    /** Called by the plugin for every damage update. */
    onDamageUpdate?: (payload: string) => void;

    /** Called by the plugin when the set of the visible NPCs changed. */
    onNpcIdsUpdate?: (payload: string) => void;

    /** Called by the plugin when NPCs entered the screen. */
    onNpcNamesUpdate?: (payload: string) => void;
    onNpcNamesClear?: (payload: string) => void;

    /** Called by the plugin for every update of the visible NPC heads. */
    onNpcMarkersUpdate?: (payload: string) => void;

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
  window.onNpcIdsUpdate = applyNpcIdsUpdate;
  window.onNpcNamesUpdate = applyNpcNamesUpdate;
  window.onNpcNamesClear = applyNpcNamesClear;
  window.onNpcMarkersUpdate = applyNpcMarkersUpdate;
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
