import { createSignal } from 'solid-js';
import type { ActorStats, DamageEvent, DamageUpdate } from './types';

/** Maximum amount of hits kept in the live feed. */
const MAX_FEED_ENTRIES = 12;

/** Age after which a hit is removed from the live feed (milliseconds). */
const FEED_TTL_MS = 5000;

/** How often the feed is checked for expired entries (milliseconds). */
const FEED_SWEEP_MS = 500;

const [feed, setFeed] = createSignal<DamageEvent[]>([]);
const [actors, setActors] = createSignal<ActorStats[]>([]);
const [connected, setConnected] = createSignal(false);

export { feed, actors, connected };

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

declare global {
  interface Window {
    /** Called by the plugin for every damage update. */
    onDamageUpdate?: (payload: string) => void;

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
