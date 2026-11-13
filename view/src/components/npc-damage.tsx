import { Index, createMemo, type Accessor } from 'solid-js';
import { damageByTarget, npcMarkerIds, npcMarkers, npcNames } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import type { TargetDamage } from '../plugin/types';
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

/**
 * The number of the slots of the overlay.
 *
 * The slots are drawn once and are never re-created: the ids of the visible
 * NPCs only fill them. A NPC that enters or leaves the screen therefore never
 * touches the elements of the other ones.
 */
const SLOT_COUNT = 30;

/**
 * The indexes of the slots.
 *
 * The list never changes, so the components of the slots are created once and
 * stay the same for the whole lifetime of the overlay.
 */
const SLOT_INDEXES = Array.from({ length: SLOT_COUNT });

/**
 * The damage that is drawn above the head of the NPC of a slot.
 *
 * The id of the NPC is looked up through an accessor, so the value follows
 * the NPC that occupies the slot, even when it changes.
 */
const Damage = (props: { id: Accessor<string | undefined> }) => {
  const damage = createMemo<TargetDamage | undefined>(() => {
    const id = props.id();
    return id === undefined ? undefined : damageByTarget[id];
  });

  const color = createMemo(() => {
    const entry = damage();
    return entry ? DAMAGE_KIND_TEXT[entry.kind] : '';
  });

  const text = createMemo(() => {
    const entry = damage();
    return entry ? formatAmount(entry.amount) : null;
  });

  return (
    <span class={`npc-damage-value ${color()} ${damage() ? '' : 'hidden'}`}>
      {text()}
    </span>
  );
};

/**
 * The number above the head of a single NPC.
 *
 * The slot owns the position of the number: the plugin sends the offsets and
 * the size of the font in plain pixels and the view only copies them into the
 * stylesheet, it does not compute anything on its own.
 *
 * The id of the NPC is the one that sits at the index of the slot in the list
 * of the visible NPCs. An empty slot has no id at all and is hidden.
 */
const NpcDamageSlot = (props: { index: number }) => {
  /**
   * The id of the NPC that occupies the slot, or undefined when the slot is
   * empty.
   */
  const id = createMemo(() => npcMarkerIds()[props.index]);

  /**
   * The marker of the NPC of the slot, or undefined when the slot is empty.
   */
  const npcMarker = createMemo(() => {
    const current = id();
    return current === undefined ? undefined : npcMarkers[current];
  });

  const offsetX = createMemo(() => npcMarker()?.x ?? 0);
  const offsetY = createMemo(() => npcMarker()?.y ?? 0);
  const scale = createMemo(() => npcMarker()?.scale ?? 1);

  /**
   * The name of the NPC of the slot, looked up by its id.
   *
   * The names live in a store of their own, so they are not part of the
   * markers and the title follows the NPC that occupies the slot.
   */
  const name = createMemo(() => {
    const current = id();
    return current === undefined ? undefined : npcNames[current];
  });

  /**
   * The slot is only drawn when it holds an NPC whose position is known.
   */
  const empty = createMemo(() => id() === undefined || !npcMarker()?.placed);

  return (
    <div
      class={`npc-damage ${empty() ? 'hidden' : ''}`}
      use:cssNpcDamageX={offsetX}
      use:cssNpcDamageY={offsetY}
      use:cssNpcDamageScale={scale}
      title={name()}
    >
      <Damage id={id} />
    </div>
  );
};

/**
 * Draws the damage the visible NPCs received above their heads.
 */
export const NpcDamageOverlay = () => (
  <div class="npc-layer">
    <Index each={SLOT_INDEXES}>
      {(_, index) => <NpcDamageSlot index={index} />}
    </Index>
  </div>
);
