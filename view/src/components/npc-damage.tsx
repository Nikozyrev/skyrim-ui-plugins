import { For, createMemo, type Accessor } from 'solid-js';
import { damageByTarget, npcMarkerIds, npcMarkers } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import { createCssPropDirective } from '../utils/css-prop-directive';

const formatAmount = (value: number) => Math.round(value).toString();

const cssNpcDamageX = createCssPropDirective('--npc-damage-x');
const cssNpcDamageY = createCssPropDirective('--npc-damage-y');
const cssNpcDamageScale = createCssPropDirective('--npc-damage-scale');

void [cssNpcDamageX, cssNpcDamageY, cssNpcDamageScale];

export type CssVarDirectiveValue = Accessor<string | number>;

declare module 'solid-js' {
  namespace JSX {
    interface Directives {
      cssNpcDamageX: CssVarDirectiveValue;
      cssNpcDamageY: CssVarDirectiveValue;
      cssNpcDamageScale: CssVarDirectiveValue;
    }
  }
}

const Damage = (props: { id: string }) => {
  const damage = createMemo(() => damageByTarget[props.id]);

  const color = createMemo(( ) => damage() ? DAMAGE_KIND_TEXT[damage().kind] : '')

  return (
    <span class={`npc-damage-value ${color()} ${damage() ? '' : 'hidden'}`}>
      {damage() ? formatAmount(damage().amount) : null}
    </span>
  );
};

/**
 * The damage a single NPC received.
 *
 * The amount is looked up by the id of the marker, which is the same id the
 * plugin sends as the target of a damage event.
 *
 * The plugin sends the plain values of the number: the two offsets and the
 * size of the font, all of them in pixels. The view only copies them into the
 * stylesheet, it does not compute anything on its own.
 */
const NpcDamage = (props: { id: string }) => {
  const npcMarker = createMemo(() => npcMarkers[props.id]);

  const offsetX = createMemo(() => npcMarker()?.x ?? 0);
  const offsetY = createMemo(() => npcMarker()?.y ?? 0);
  const scale = createMemo(() => npcMarker()?.scale ?? 1);

  return (
    <div
      class={`npc-damage ${npcMarker()?.placed ? '' : 'hidden'}`}
      use:cssNpcDamageX={offsetX}
      use:cssNpcDamageY={offsetY}
      use:cssNpcDamageScale={scale}
      title={npcMarker()?.name}
    >
      <Damage id={props.id} />
    </div>
  );
};

/**
 * Draws the damage the visible NPCs received above their heads.
 */
export function NpcDamageOverlay() {
  return (
    <div class="npc-layer">
      <For each={npcMarkerIds()}>{(id) => <NpcDamage id={id} />}</For>
    </div>
  );
}
