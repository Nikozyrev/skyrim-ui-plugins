/**
 * The values that the marker of a NPC carries.
 *
 * The values live in the custom properties of the document, so they are
 * addressed by the name of their property.
 */
export type MarkerValue = 'x' | 'y' | 'scale' | 'placed';

/**
 * The name of the custom property that holds one value of one slot.
 *
 * Every value of a marker lives in a custom property of the document that is
 * addressed by the number of its slot, so the view finds the values of a slot
 * with the number it already knows: `--npc-slot-3-x`.
 *
 * The number of the slot is used instead of the id of the NPC, because the
 * plugin owns the slots: the id of an NPC changes when it leaves the screen
 * and comes back, the number of its slot does not have to.
 */
export const markerProperty = (slot: number, value: MarkerValue) => `--npc-slot-${slot}-${value}`;
