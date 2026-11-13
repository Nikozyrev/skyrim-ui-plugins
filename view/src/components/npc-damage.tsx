import {
  For,
  Show,
  createMemo,
  createSignal,
  onCleanup,
  onMount,
  type Accessor,
} from 'solid-js';
import { damageByTarget, npcMarkerIds, npcMarkers } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import type { NpcMarker } from '../plugin/types';
import { createCssPropDirective } from '../utils/css-prop-directive';

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

/**
 * The font size of a damage number before it is scaled, in CSS pixels.
 *
 * The stylesheet keeps the text at this size and the scale does the rest, so
 * the number grows around the anchor instead of pushing it around.
 */
const BASE_FONT_SIZE = 11;

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
 * The horizontal offset of a damage number.
 *
 * The plugin sends normalized coordinates: x runs from the left to the right
 * edge of the screen, y from the bottom to the top one. Only the horizontal
 * part is built here, the vertical one lives in `buildTransformY`.
 *
 * The value is a plain number, the pixels are added by the stylesheet.
 */
const buildTransformX = (marker: NpcMarker, view: Viewport): number =>
  marker.x * view.width;

/**
 * The vertical offset of a damage number.
 *
 * The number is placed right above the head of the NPC, so the offset is
 * shifted up by the radius of the head and the lower edge of the number
 * touches the anchor.
 *
 * The value is a plain number, the pixels are added by the stylesheet.
 */
const buildTransformY = (marker: NpcMarker, view: Viewport): number => {
  const y = (1 - marker.y) * view.height;
  const radius = Math.max(marker.r * view.height, MIN_RADIUS);

  return y - radius;
};

/**
 * The scale of a damage number.
 *
 * It follows the size of the head on the screen, so a number of an NPC that
 * is far away stays small, but it never becomes unreadable.
 *
 * The value is a plain number, the stylesheet applies it to the transform.
 */
const buildScale = (marker: NpcMarker, view: Viewport): number => {
  const radius = Math.max(marker.r * view.height, MIN_RADIUS);
  const size = Math.min(MAX_FONT_SIZE, Math.max(MIN_FONT_SIZE, radius * 0.9));

  return size / BASE_FONT_SIZE;
};

const formatAmount = (value: number) => Math.round(value).toString();

const cssNpcDamageX = createCssPropDirective("--npc-damage-x");
const cssNpcDamageY = createCssPropDirective("--npc-damage-y");
const cssNpcDamageScale = createCssPropDirective("--npc-damage-scale");

void [cssNpcDamageX, cssNpcDamageY, cssNpcDamageScale];

export type CssVarDirectiveValue = Accessor<string | number>;

declare module "solid-js" {
  namespace JSX {
    interface Directives {
      cssNpcDamageX: CssVarDirectiveValue;
      cssNpcDamageY: CssVarDirectiveValue;
      cssNpcDamageScale: CssVarDirectiveValue;
    }
  }
}

const Damage = (props: { id: string }) => {
  const targetDamage = createMemo(() => damageByTarget[props.id]);

  return (
    <Show when={targetDamage()}>
      {(damage) => (
        <span class={`npc-damage-value ${DAMAGE_KIND_TEXT[damage().kind]}`}>
          {formatAmount(damage().amount)}
        </span>
      )}
    </Show>
  );
};

/**
 * The damage a single NPC received.
 *
 * The amount is looked up by the id of the marker, which is the same id the
 * plugin sends as the target of a damage event.
 */
const NpcDamage = (props: { id: string }) => {
  const npcMarker = createMemo(() => npcMarkers[props.id]);

  const transformX = createMemo(() =>
    npcMarker() ? buildTransformX(npcMarker(), viewport()) : 0,
  );
  const transformY = createMemo(() =>
    npcMarker() ? buildTransformY(npcMarker(), viewport()) : 0,
  );
  const scale = createMemo(() =>
    npcMarker() ? buildScale(npcMarker(), viewport()) : 1,
  );

  return (
    <Show when={npcMarker()?.placed ? npcMarker() : undefined}>
      {(marker) => (
        <div
          class="npc-damage"
          use:cssNpcDamageX={transformX}
          use:cssNpcDamageY={transformY}
          use:cssNpcDamageScale={scale}
          title={marker().name}
        >
          <Damage id={props.id} />
        </div>
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
