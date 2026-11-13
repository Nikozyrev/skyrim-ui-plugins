/**
 * The values that the marker of a NPC carries.
 *
 * The values live in the custom properties of the document, so they are
 * addressed by the name of their property.
 */
export type MarkerValue = 'x' | 'y' | 'scale' | 'placed';

/**
 * The name of the custom property that holds one value of one NPC.
 *
 * Every value of a marker lives in a custom property of the document that is
 * addressed by the id of the NPC, so the view finds the values of a NPC with
 * the id it already knows: `--npc-0001ABCD-x`.
 */
export const markerProperty = (id: string, value: MarkerValue) => `--npc-${id}-${value}`;
