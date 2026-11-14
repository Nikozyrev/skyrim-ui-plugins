import { Index, createMemo, onCleanup, onMount, type Accessor } from 'solid-js';
import { damageByTarget, npcNames, npcSlots, slotIndexes } from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import { registerSlotElement, unregisterSlotElement } from '../plugin/slot-elements';
import type { TargetDamage } from '../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

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
 * The plugin owns the slots: it hands a free one to every NPC that enters the
 * screen and takes it back when the NPC leaves. The id and the name of the
 * NPC are therefore looked up by the number of the slot. An empty slot has no
 * NPC at all and is hidden.
*/
const NpcDamageSlot = (props: { index: number }) => {
  /**
   * The id of the NPC that occupies the slot, or undefined when the slot is
   * empty.
   */
  const id = createMemo(() => npcSlots[props.index]);

  /**
   * The name of the NPC of the slot, looked up by its id.
   *
   * The names live in a store of their own, so they are not part of the
   * slots and the title follows the NPC that occupies the slot.
   */
  const name = createMemo(() => {
    const current = id();
    return current === undefined ? undefined : npcNames[current];
  });

  /**
   * The values of the slot.
   *
   * The values of a slot are written into the element of the slot itself, so
   * the element only has to be known to the plugin. The plugin looks it up by
   * the number of the slot, which is why the element is registered here when
   * the component is mounted.
   */
  let element: HTMLDivElement | undefined;

  onMount(() => {
    if (element) {
      registerSlotElement(props.index, element);
    }
  });

  onCleanup(() => {
    if (element) {
      unregisterSlotElement(props.index, element);
    }
  });

  return (
    <div
      class="npc-damage"
      ref={(el) => {
        element = el;
      }}
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
    <Index each={slotIndexes()}>
      {(_, index) => <NpcDamageSlot index={index} />}
    </Index>
  </div>
);
