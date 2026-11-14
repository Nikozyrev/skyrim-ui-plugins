import { Index, createMemo, type Accessor } from 'solid-js';
import { damageByTarget, npcMarkerIds, npcNames } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import { markerProperty, type MarkerValue } from '../plugin/marker-properties';
import type { TargetDamage } from '../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

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

const markerValue = (id: string | undefined, value: MarkerValue, fallback: string) => {
  return id === undefined ? fallback : `var(${markerProperty(id, value)}, ${fallback})`;
};

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
   * One value of the NPC, as it is written into the custom property of the
   * slot.
   *
   * The values of a NPC live in the global custom properties of the document,
   * so the slot only points at them: the property of the slot follows the NPC
   * that occupies it. A slot without an NPC keeps the fallback, so it is
   * drawn nowhere.
   */
  const style = createMemo(() => ({
    '--npc-damage-x': markerValue(id(), 'x', '0'),
    '--npc-damage-y': markerValue(id(), 'y', '0'),
    '--npc-damage-scale': markerValue(id(), 'scale', '1'),
    '--npc-damage-placed': markerValue(id(), 'placed', '0'),
  }));

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

  return (
    <div
      class="npc-damage"
      style={style()}
    >
      <div class="w-fit text-center text-white">{name()}</div>
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
