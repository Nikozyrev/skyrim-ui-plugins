/**
 * The values of a marker, as they are written into the styles of a slot.
 *
 * The values are the plain CSS values of the number above the head: the two
 * offsets in whole pixels and the scale of the font.
 */
type MarkerStyle = {
  /** The horizontal offset of the number, in pixels. */
  x: string;

  /** The vertical offset of the number, in pixels. */
  y: string;

  /** The scale of the font of the number. */
  scale: string;
};

/**
 * The root element of every slot of the overlay, keyed by the number of the
 * slot.
 *
 * The plugin owns the slots: it hands a free one to every NPC that enters the
 * screen and takes it back when the NPC leaves. The component of a slot
 * registers its element here when it is mounted, so the values of a slot are
 * written into the element of that slot and not into the custom properties of
 * the document.
 */
const slotElements = new Map<number, HTMLElement>();

/**
 * The values that were written into a slot last, keyed by the number of the
 * slot.
 *
 * The values are kept, so a slot whose element is mounted after the plugin
 * already sent the position of its NPC is drawn right away.
 */
const slotStyles = new Map<number, MarkerStyle>();

/**
 * Writes the values of a marker into the element of its slot.
 *
 * The element of the slot is found by the number of the slot, so the values
 * are written into the element itself. The stylesheet resolves them, so a
 * frame does not have to touch the elements of the slots that did not move.
 */
export const setMarkerValues = (
  slot: number,
  x: string,
  y: string,
  scale: string,
) => {
  const style: MarkerStyle = { x, y, scale };

  slotStyles.set(slot, style);

  const element = slotElements.get(slot);

  if (!element) {
    return;
  }

  applyStyle(element, style);
};

/**
 * Drops the values of a slot.
 *
 * The properties are removed instead of being reset, so an NPC that comes back
 * to the screen starts without a position again.
 */
export const clearMarkerValues = (a_slot: number) => {
  slotStyles.delete(a_slot);

  const element = slotElements.get(a_slot);

  if (!element) {
    return;
  }

  element.style.removeProperty('--npc-damage-x');
  element.style.removeProperty('--npc-damage-y');
  element.style.removeProperty('--npc-damage-scale');
};

/**
 * Registers the element of a slot.
 *
 * The values of the slot are written into the element right away, so a slot
 * that is mounted after the plugin sent the position of its NPC is drawn
 * where it belongs.
 */
export const registerSlotElement = (a_slot: number, a_element: HTMLElement) => {
  slotElements.set(a_slot, a_element);

  const style = slotStyles.get(a_slot);

  if (!style) {
    return;
  }

  applyStyle(a_element, style);
};

/**
 * Forgets the element of a slot.
 *
 * The element is only forgotten when it is still the one that is registered,
 * so a slot that was already taken over by another element keeps its values.
 */
export const unregisterSlotElement = (
  a_slot: number,
  a_element: HTMLElement,
) => {
  if (slotElements.get(a_slot) === a_element) {
    slotElements.delete(a_slot);
  }
};

/**
 * Puts the values of a marker into the styles of the element of its slot.
 */
const applyStyle = (a_element: HTMLElement, a_style: MarkerStyle) => {
  a_element.style.setProperty('--npc-damage-x', a_style.x);
  a_element.style.setProperty('--npc-damage-y', a_style.y);
  a_element.style.setProperty('--npc-damage-scale', a_style.scale);
};
