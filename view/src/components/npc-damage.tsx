import { For, Show, createSignal, onCleanup, onMount } from 'solid-js';
import { damageByTarget, npcMarkerIds, npcMarkers } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import type { NpcMarker } from '../plugin/types';

/**
 * Smallest radius of a head on the screen, in CSS pixels.
 *
 * It only keeps the offset above the head from collapsing for NPCs that are
 * far away.
 */
const MIN_RADIUS = 2;

/** Bounds of the font size of a damage number, in CSS pixels. */
const MIN_FONT_SIZE = 11;
const MAX_FONT_SIZE = 34;

type Viewport = { width: number; height: number };

const readViewport = (): Viewport => ({
  width: window.innerWidth,
  height: window.innerHeight,
});

/**
 * The viewport of the view.
 *
 * It is a module level signal, so every marker shares the same value.
 */
const [viewport, setViewport] = createSignal<Viewport>(readViewport());

/**
 * Builds the CSS transform of a damage number.
 *
 * The plugin sends normalized coordinates: x runs from the left to the right
 * edge of the screen, y from the bottom to the top one. The number is placed
 * right above the head of the NPC:
 *
 *   translate(x, y)     - places the number on the screen;
 *   translate(-50%,-100%) - centers it horizontally and puts its lower edge
 *                         on the anchor.
 */
const buildTransform = (marker: NpcMarker, view: Viewport): string => {
  const x = marker.x * view.width;
  const y = (1 - marker.y) * view.height;
  const radius = Math.max(marker.r * view.height, MIN_RADIUS);

  return `translate(${x}px, ${y - radius}px) translate(-50%, -100%)`;
};

/**
 * The font size of a damage number.
 *
 * It follows the size of the head on the screen, so a number of an NPC that
 * is far away stays small, but it never becomes unreadable.
 */
const buildFontSize = (marker: NpcMarker, view: Viewport): number => {
  const radius = Math.max(marker.r * view.height, MIN_RADIUS);

  return Math.min(MAX_FONT_SIZE, Math.max(MIN_FONT_SIZE, radius * 0.9));
};

const formatAmount = (value: number) => Math.round(value).toString();

/**
 * The damage a single NPC received.
 *
 * The amount is looked up by the id of the marker, which is the same id the
 * plugin sends as the target of a damage event.
 */
const NpcDamage = (props: { id: string }) => {
  const marker = () => npcMarkers().get(props.id);
  const damage = () => damageByTarget().get(props.id);

  return (
    <Show when={marker()}>
      {(current) => (
        <Show when={damage()}>
          {(taken) => (
            <div
              class="npc-damage"
              style={{
                transform: buildTransform(current(), viewport()),
                'font-size': `${buildFontSize(current(), viewport())}px`,
              }}
              title={current().name}
            >
              <Show when={taken().amount}>
                {(amount) => (
                  <span class={`npc-damage-value ${DAMAGE_KIND_TEXT[taken().kind]}`}>
                    {formatAmount(amount())}
                  </span>
                )}
              </Show>
            </div>
          )}
        </Show>
      )}
    </Show>
  );
};

/**
 * Draws the damage the visible NPCs received above their heads.
 */
export function NpcDamageOverlay() {
  onMount(() => {
    const onResize = () => setViewport(readViewport());

    window.addEventListener('resize', onResize);
    onCleanup(() => window.removeEventListener('resize', onResize));
  });

  return (
    <div class="npc-layer">
      <For each={npcMarkerIds()}>{(id) => <NpcDamage id={id} />}</For>
    </div>
  );
}
