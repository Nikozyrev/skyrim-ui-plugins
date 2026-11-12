import { For, createSignal, onCleanup, onMount } from 'solid-js';
import { npcMarkers } from '../plugin/plugin';
import type { NpcMarker } from '../plugin/types';

/**
 * Diameter of the marker circle in CSS pixels before it is scaled.
 *
 * The circle is sized with a CSS transform, so this value only decides how
 * precise the scaling is - the final size comes from the plugin.
 */
const BASE_SIZE = 100;

/** Smallest radius of a marker in CSS pixels. */
const MIN_RADIUS = 2;

type Viewport = { width: number; height: number };

const readViewport = (): Viewport => ({
  width: window.innerWidth,
  height: window.innerHeight,
});

/**
 * Builds the CSS transform of a marker.
 *
 * The plugin sends normalized coordinates: x runs from the left to the right
 * edge of the screen, y from the bottom to the top one. Position and size
 * travel in a single transform:
 *
 *   translate(x, y) - places the circle on the screen;
 *   scale(...)      - gives the circle its size;
 *   translate(-50%) - moves the circle onto its own origin.
 */
const buildTransform = (marker: NpcMarker, viewport: Viewport): string => {
  const x = marker.x * viewport.width;
  const y = (1 - marker.y) * viewport.height;
  const radius = Math.max(marker.r * viewport.height, MIN_RADIUS);
  const scale = radius / (BASE_SIZE / 2);

  return `translate(${x}px, ${y}px) scale(${scale}) translate(-50%, -50%)`;
};

export function NpcHeads() {
  const [viewport, setViewport] = createSignal<Viewport>(readViewport());

  onMount(() => {
    const onResize = () => setViewport(readViewport());

    window.addEventListener('resize', onResize);
    onCleanup(() => window.removeEventListener('resize', onResize));
  });

  return (
    <div class="npc-layer">
      <For each={npcMarkers()}>
        {(marker) => (
          <div
            class="npc-marker"
            style={{ transform: buildTransform(marker, viewport()) }}
            title={marker.name}
          />
        )}
      </For>
    </div>
  );
}
