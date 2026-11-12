import { createSignal } from 'solid-js';
import { dominantKind, resolveKind } from './damage-colors';
import type {
  ActorStats,
  DamageEvent,
  DamageUpdate,
  NpcIdsUpdate,
  NpcMarker,
  NpcMarkersUpdate,
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
 * head of the NPC.
 */
const [npcMarkers, setNpcMarkers] = createSignal<Map<string, NpcMarker>>(new Map());

/**
 * The damage every target received, keyed by the form id of the target.
 *
 * The ids are the ones the plugin sends as the `targetId` of a damage event
 * and as the `id` of an NPC marker, so the overlay can look the damage of a
 * marker up directly.
 */
const [damageByTarget, setDamageByTarget] = createSignal<Map<string, TargetDamage>>(new Map());

export { feed, actors, connected, npcMarkerIds, npcMarkers, damageByTarget };

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
 * Adds the damage of a single event to the damage map.
 *
 * The amount of a target is the sum of all the hits it took, so the number
 * grows with every hit. The kind of the most recent hit is kept as well, it
 * colours the number.
 */
const addDamage = (previous: Map<string, TargetDamage>, a_event: DamageEvent): Map<string, TargetDamage> => {
  const next = new Map(previous);

  const current = next.get(a_event.targetId);
  const entry: TargetDamage = {
    amount: (current?.amount ?? 0) + a_event.damage,
    kind: resolveKind(a_event.kind),
  };

  // Re-inserting the entry moves the target to the end of the map, so the
  // oldest entry is the one that was hit the longest time ago.
  next.delete(a_event.targetId);
  next.set(a_event.targetId, entry);

  // The oldest entry is dropped when the map grows past its budget.
  while (next.size > MAX_DAMAGE_TARGETS) {
    const oldest = next.keys().next();
    if (oldest.done) {
      break;
    }
    next.delete(oldest.value);
  }

  return next;
};

/**
 * Builds the damage map out of a complete state.
 *
 * The aggregates of the plugin hold the damage every actor took over the
 * whole session, so they are the base of the map. The hits of the actors that
 * are not part of the aggregates are added from the events.
 */
const buildDamageMap = (a_actors: ActorStats[], a_events: DamageEvent[]): Map<string, TargetDamage> => {
  const map = new Map<string, TargetDamage>();

  for (const actor of a_actors) {
    if (actor.damageTaken <= 0) {
      continue;
    }

    map.set(actor.id, {
      amount: actor.damageTaken,
      kind: dominantKind(actor.takenByKind),
    });
  }

  // The hits of an actor that is part of the aggregates are already counted.
  const aggregated = new Set(map.keys());

  for (const event of a_events) {
    if (aggregated.has(event.targetId)) {
      continue;
    }

    const current = map.get(event.targetId);
    map.set(event.targetId, {
      amount: (current?.amount ?? 0) + event.damage,
      kind: resolveKind(event.kind),
    });
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

    // A snapshot carries the complete state, so the damage map is rebuilt
    // instead of being extended.
    setDamageByTarget(
      buildDamageMap(
        Array.isArray(update.actors) ? update.actors : [],
        Array.isArray(update.events) ? update.events : [],
      ),
    );

    return;
  }

  const event = update.event;
  if (event) {
    setFeed((previous) => [event, ...previous].slice(0, MAX_FEED_ENTRIES));
    setDamageByTarget((previous) => addDamage(previous, event));
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

const isFiniteNumber = (value: unknown): value is number =>
  typeof value === 'number' && Number.isFinite(value);

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

const parseNpcMarkersUpdate = (payload: unknown): NpcMarkersUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as NpcMarkersUpdate;
    if (!parsed || typeof parsed !== 'object' || parsed.type !== 'npcMarkers') {
      return null;
    }
    return parsed;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

/**
 * Applies the ids of the NPCs that are currently visible on the screen.
 *
 * The plugin only sends the ids when an NPC entered or left the screen, so
 * the list is replaced with the complete set of the visible NPCs.
 */
const applyNpcIdsUpdate = (payload: string) => {
  const update = parseNpcIdsUpdate(payload);
  if (!update) {
    return;
  }

  const ids = Array.isArray(update.ids)
    ? update.ids.filter((id): id is string => typeof id === 'string' && id.length > 0)
    : [];

  setNpcMarkerIds(ids);
};

/**
 * Applies the heads of the NPCs that are currently visible on the screen.
 *
 * The payload replaces the previous markers completely, so an NPC that left
 * the screen disappears without a separate "remove" message.
 */
const applyNpcMarkersUpdate = (payload: string) => {
  const update = parseNpcMarkersUpdate(payload);
  if (!update) {
    return;
  }

  const markers = new Map<string, NpcMarker>();

  if (update.markers && typeof update.markers === 'object') {
    for (const [id, marker] of Object.entries(update.markers)) {
      if (!id || !marker || !isFiniteNumber(marker.x) || !isFiniteNumber(marker.y) || !isFiniteNumber(marker.r)) {
        continue;
      }

      if (markers.has(id)) {
        continue;
      }

      markers.set(id, {
        id,
        name: typeof marker.name === 'string' ? marker.name : '',
        x: marker.x,
        y: marker.y,
        r: marker.r,
      });
    }
  }

  setNpcMarkers(markers);
};

declare global {
  interface Window {
    /** Called by the plugin for every damage update. */
    onDamageUpdate?: (payload: string) => void;

    /** Called by the plugin when the set of the visible NPCs changed. */
    onNpcIdsUpdate?: (payload: string) => void;

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
