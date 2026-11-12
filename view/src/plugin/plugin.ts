import { createSignal } from 'solid-js';
import type { ActorStats, DamageEvent, DamageUpdate, NpcMarker, NpcUpdate } from './types';

/** Maximum amount of hits kept in the live feed. */
const MAX_FEED_ENTRIES = 12;

/** Age after which a hit is removed from the live feed (milliseconds). */
const FEED_TTL_MS = 5000;

/** How often the feed is checked for expired entries (milliseconds). */
const FEED_SWEEP_MS = 500;

const [feed, setFeed] = createSignal<DamageEvent[]>([]);
const [actors, setActors] = createSignal<ActorStats[]>([]);
const [connected, setConnected] = createSignal(false);
const [npcMarkers, setNpcMarkers] = createSignal<NpcMarker[]>([]);

export { feed, actors, connected, npcMarkers };

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

const applyUpdate = (payload: string) => {
  const update = parseUpdate(payload);
  if (!update) {
    return;
  }

  setConnected(true);
  setActors(Array.isArray(update.actors) ? update.actors : []);

  if (update.type === 'snapshot') {
    setFeed(Array.isArray(update.events) ? update.events.slice(0, MAX_FEED_ENTRIES) : []);
    return;
  }

  const event = update.event;
  if (event) {
    setFeed((previous) => [event, ...previous].slice(0, MAX_FEED_ENTRIES));
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

const parseNpcUpdate = (payload: unknown): NpcUpdate | null => {
  if (typeof payload !== 'string' || payload.length === 0) {
    return null;
  }

  try {
    const parsed = JSON.parse(payload) as NpcUpdate;
    if (!parsed || typeof parsed !== 'object' || parsed.type !== 'npc') {
      return null;
    }
    return parsed;
  } catch {
    // A malformed payload must never break the overlay.
    return null;
  }
};

/**
 * Applies the heads of the NPCs that are currently visible on the screen.
 *
 * The payload replaces the previous markers completely, so an NPC that left
 * the screen disappears without a separate "remove" message.
 */
const applyNpcUpdate = (payload: string) => {
  const update = parseNpcUpdate(payload);
  if (!update) {
    return;
  }

  const markers: NpcMarker[] = [];

  if (Array.isArray(update.markers)) {
    for (const marker of update.markers) {
      if (!marker || !isFiniteNumber(marker.x) || !isFiniteNumber(marker.y) || !isFiniteNumber(marker.r)) {
        continue;
      }

      markers.push({
        id: typeof marker.id === 'string' ? marker.id : '',
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

    /** Called by the plugin for every update of the visible NPC heads. */
    onNpcUpdate?: (payload: string) => void;

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
  window.onNpcUpdate = applyNpcUpdate;

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
