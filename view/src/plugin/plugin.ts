import { batch, createSignal } from 'solid-js';
import { createStore, reconcile } from 'solid-js/store';
import { dominantKind, resolveKind } from './damage-colors';
import type {
  ActorStats,
  DamageEvent,
  DamageUpdate,
  NpcIdsUpdate,
  NpcMarker,
  NpcNamesUpdate,
  TargetDamage,
} from './types';

/** Maximum amount of hits kept in the live feed. */
const MAX_FEED_ENTRIES = 12;

/** Age after which a hit is removed from the live feed (milliseconds). */
const FEED_TTL_MS = 5000;

/** How often the feed is checked for expired entries (milliseconds). */
const FEED_SWEEP_MS = 500;

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
 * The markers of the visible NPCs, keyed by the form id of the NPC.
 *
 * The markers arrive with every frame, they only carry the position of the
 * head of the NPC. They live in a store instead of a signal, so a frame only
 * touches the markers that really moved and only those are drawn again.
 */
const [npcMarkers, setNpcMarkers] = createStore<Record<string, NpcMarker>>({});

/**
 * The ids of the visible NPCs in the order of the frames.
 *
 * The frames address their markers by the index of the NPC in this list, so
 * the list is kept next to the store and not inside it.
 */
let npcOrder: string[] = [];

/**
 * The names of the NPCs that are visible on the screen, keyed by their id.
 *
 * The plugin sends the names of the NPCs that entered the screen in their own
 * message, so they are kept next to the markers and not inside the store.
 */
const npcNames = new Map<string, string>();

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

export { feed, actors, connected, npcMarkerIds, npcMarkers, damageByTarget };

/**
 * The number of steps the normalized coordinates are scaled to.
 *
 * The plugin sends the positions as integers, scaled by this factor. It
 * mirrors `Npc::QuantizedMarker::kScale` of the plugin.
 */
const POSITION_SCALE = 10000;

/**
 * The margin around the viewport that is still drawn.
 *
 * The plugin shifts the positions by this margin before it quantizes them, so
 * a quantized position is never negative. It mirrors
 * `Npc::QuantizedMarker::kMargin` of the plugin.
 */
const POSITION_MARGIN = 0.05;

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

/**
 * Applies the names of the NPCs that entered the screen.
 *
 * The plugin sends the names in their own message and only for the NPCs that
 * the view does not know yet, so the names of the visible NPCs are kept in a
 * map of their own.
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

      npcNames.set(id, name);

      // The marker of the NPC may already exist, then its name is updated
      // right away. Otherwise the name is picked up when the ids arrive.
      if (npcMarkers[id]) {
        setNpcMarkers(id, 'name', name);
      }
    }
  });
};

/**
 * Applies the ids of the NPCs that are currently visible on the screen.
 *
 * The plugin only sends the ids when an NPC entered or left the screen, so
 * the list is replaced with the complete set of the visible NPCs. The order
 * of the list matters: the frames of the positions address their markers by
 * the index of the NPC in it.
 */
const applyNpcIdsUpdate = (payload: string) => {
  const update = parseNpcIdsUpdate(payload);
  if (!update) {
    return;
  }

  const ids = Array.isArray(update.ids)
    ? update.ids.filter((id): id is string => typeof id === 'string' && id.length > 0)
    : [];

  npcOrder = ids;
  setNpcMarkerIds(ids);

  // The markers that stay keep their position, the new ones start without a
  // position and the ones that left the screen are dropped.
  const next: Record<string, NpcMarker> = {};

  for (const id of ids) {
    const current = npcMarkers[id];

    next[id] = {
      id,
      name: npcNames.get(id) ?? current?.name ?? '',
      x: current?.x ?? 0,
      y: current?.y ?? 0,
      r: current?.r ?? 0,
      placed: current?.placed ?? false,
    };
  }

  setNpcMarkers(reconcile(next));

  // The names of the NPCs that left the screen are dropped as well.
  for (const id of Array.from(npcNames.keys())) {
    if (!next[id]) {
      npcNames.delete(id);
    }
  }
};

/**
 * Applies the heads of the NPCs that are currently visible on the screen.
 *
 * A frame is a list of the tuples "index:x,y,r", separated by semicolons. The
 * index is the position of the NPC in the list of the ids, the numbers are the
 * quantized positions of the head.
 *
 * Only the markers that really moved are part of a frame, so the markers that
 * did not move keep the position they already have.
 */
const applyNpcMarkersUpdate = (payload: string) => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return;
  }

  batch(() => {
    for (const chunk of payload.split(';')) {
      if (!chunk) {
        continue;
      }

      const separator = chunk.indexOf(':');
      if (separator <= 0) {
        continue;
      }

      const id = npcOrder[Number(chunk.slice(0, separator))];
      if (!id || !npcMarkers[id]) {
        continue;
      }

      const values = chunk.slice(separator + 1).split(',');
      if (values.length < 3) {
        continue;
      }

      const x = Number(values[0]) / POSITION_SCALE - POSITION_MARGIN;
      const y = Number(values[1]) / POSITION_SCALE - POSITION_MARGIN;
      const r = Number(values[2]) / POSITION_SCALE;

      if (!Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(r)) {
        continue;
      }

      setNpcMarkers(id, 'x', x);
      setNpcMarkers(id, 'y', y);
      setNpcMarkers(id, 'r', r);
      setNpcMarkers(id, 'placed', true);
    }
  });
};

declare global {
  interface Window {
    /** Called by the plugin for every damage update. */
    onDamageUpdate?: (payload: string) => void;

    /** Called by the plugin when the set of the visible NPCs changed. */
    onNpcIdsUpdate?: (payload: string) => void;

    /** Called by the plugin when NPCs entered the screen. */
    onNpcNamesUpdate?: (payload: string) => void;

    /** Called by the plugin for every update of the visible NPC heads. */
    onNpcMarkersUpdate?: (payload: string) => void;

    /** Called by the view to tell the plugin that it is ready to receive data. */
    onViewReady?: () => void;
  }
}

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
  window.onNpcMarkersUpdate = applyNpcMarkersUpdate;

  window.setInterval(sweepFeed, FEED_SWEEP_MS);

  const announce = () => {
    window.onViewReady?.();
  };

  // The first call may race with the plugin registering its listener, so the
  // announcement is repeated a few times.
  window.setTimeout(announce, 0);
  window.setTimeout(announce, 250);
  window.setTimeout(announce, 1000);
};
