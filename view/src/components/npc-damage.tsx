import {
  Index,
  createEffect,
  createMemo,
  onCleanup,
  onMount,
  type Accessor,
} from 'solid-js';
import {
  damageByTarget,
  healthByNpc,
  npcNames,
  npcSlots,
  slotIndexes,
} from '../plugin/plugin';
import { DAMAGE_KIND_TEXT } from '../plugin/damage-colors';
import {
  clearHealthValues,
  registerSlotElement,
  setHealthValues,
  unregisterSlotElement,
} from '../plugin/slot-elements';
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
 * The bar of the health of the NPC of a slot.
 *
 * The bar itself is drawn by the stylesheet: the current health and the
 * maximum health of the NPC are written into the styles of the slot as
 * custom properties, so the stylesheet calculates the percentage of the bar
 * on its own.
 */
const HealthBar = (props: { slotIndex: number; id: Accessor<string | undefined> }) => {
  const health =  createMemo(() => {
    const id = props.id();
    return id === undefined ? undefined : healthByNpc[id];
  });

  /**
   * Puts the health of the NPC into the styles of the slot.
   *
   * The stylesheet resolves the two values into the percentage of the bar,
   * so only the slot of the NPC that took the damage is touched.
   */
  createEffect(() => {
    const entry = health();

    if (!entry) {
      clearHealthValues(props.slotIndex);
      return;
    }

    setHealthValues(props.slotIndex, String(entry.current), String(entry.max));
  });


  return (
    <div class={`npc-health ${health() ? '' : 'hidden'}`}>
      <div class="npc-health-fill" />
    </div>
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
      <div class="npc-damage-stack">
        <div class="npc-damage-name">{name()}</div>
        <HealthBar id={id} slotIndex={props.index} />
        <Damage id={id} />
      </div>
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
