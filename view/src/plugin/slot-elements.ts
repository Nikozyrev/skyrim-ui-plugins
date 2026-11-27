/**
 * The values of a marker, as they are written into the styles of a slot.
 *
 * The values are the plain CSS values of the number above the head: the two
 * offsets in pixels and the scale of the font.
 *
 * The plugin calculates the future on its own: the values that arrive here
 * are already the ones the marker will have when the game frame they belong
 * to is on the screen. The view only writes them into the element.
 */
type MarkerStyle = {
  /** The horizontal offset of the number, in pixels. */
  x: string;

  /** The vertical offset of the number, in pixels. */
  y: string;

  /** The scale of the font of the number. */
  scale: string;

  /**
   * The moment the plugin measured the position.
   *
   * The time is a part of the values of a slot: it says how old the
   * positions of the frame are, so the view can report the delay of the
   * whole path to the plugin.
   */
  time: number;
};

/**
 * The health of an NPC, as it is written into the styles of a slot.
 *
 * The values are the plain CSS values of the health bar: the health the NPC
 * has right now and the health it can have at most. The stylesheet turns
 * them into the percentage of the bar.
 */
type HealthStyle = {
  /** The health the NPC has right now. */
  current: string;

  /** The health the NPC can have at most. */
  max: string;
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
 *
 * The map is also the place where the values of a frame are collected: a slot
 * that is in the frame more than once keeps the values of its newest tuple,
 * so the older ones are never written into the element.
 */
const slotStyles = new Map<number, MarkerStyle>();

/**
 * The slots whose values changed since the styles were written last.
 *
 * The values of a frame are collected first and written into the elements
 * afterwards, so a slot is written once per frame, with its newest values.
 */
const dirtySlots = new Set<number>();

/**
 * The health that was written into a slot last, keyed by the number of the
 * slot.
 *
 * The health is kept as well, so a slot whose element is mounted after the
 * health of its NPC arrived is drawn right away.
 */
const slotHealth = new Map<number, HealthStyle>();

/**
 * Collects the values of a marker.
 *
 * The values are not written into the element right away: they are kept for
 * the slot and the slot is marked as changed. The elements are written by
 * `flushMarkerValues`, so a slot that is in a frame more than once is written
 * once, with its newest values.
 *
 * The time is the moment the plugin measured the position. It is needed to
 * tell how old the position is.
 */
export const setMarkerValues = (
  slot: number,
  x: string,
  y: string,
  scale: string,
  time: number,
) => {
  const known = slotStyles.get(slot);

  if (
    known &&
    known.x === x &&
    known.y === y &&
    known.scale === scale &&
    known.time === time
  ) {
    // The values are the same as the ones the slot already has, so the
    // element does not have to be touched at all.
    return;
  }

  slotStyles.set(slot, { x, y, scale, time });

  dirtySlots.add(slot);
};

/**
 * Writes the values of the slots that changed into their elements.
 *
 * The element of a slot is found by the number of the slot, so the values are
 * written into the element itself. The stylesheet resolves them, so a frame
 * does not have to touch the elements of the slots that did not move.
 *
 * The values that are written are the ones the marker will have when the
 * game frame it belongs to is on the screen: the plugin moves every marker
 * forward by the length of the frame the view is behind, so the view does not
 * have to calculate anything on its own.
 *
 * @return the number of milliseconds that the positions of the newest
 *         measurement needed until they were on the screen
 */
export const flushMarkerValues = (): number => {
  if (dirtySlots.size === 0) {
    return 0;
  }

  const now = Date.now();

  let newest = 0;

  for (const slot of dirtySlots) {
    const element = slotElements.get(slot);
    const style = slotStyles.get(slot);

    if (element && style) {
      applyStyle(element, style);
    }

    if (style && style.time > newest) {
      newest = style.time;
    }
  }

  dirtySlots.clear();

  if (newest === 0) {
    return 0;
  }

  return Math.max(0, now - newest);
};

/**
 * Writes the health of an NPC into the element of its slot.
 *
 * The health is written as two custom properties, so the stylesheet
 * calculates the percentage of the bar on its own.
 */
export const setHealthValues = (
  slot: number,
  current: string,
  max: string,
) => {
  const style: HealthStyle = { current, max };

  slotHealth.set(slot, style);

  const element = slotElements.get(slot);

  if (!element) {
    return;
  }

  applyHealth(element, style);
};

/**
 * Drops the health of a slot.
 *
 * The properties are removed instead of being reset, so an NPC that comes back
 * to the screen starts without a health bar again.
 */
export const clearHealthValues = (a_slot: number) => {
  slotHealth.delete(a_slot);

  const element = slotElements.get(a_slot);

  if (!element) {
    return;
  }

  element.style.removeProperty('--npc-health-current');
  element.style.removeProperty('--npc-health-max');
};

/**
 * Drops the values of a slot.
 *
 * The properties are removed instead of being reset, so an NPC that comes back
 * to the screen starts without a position again.
 */
export const clearMarkerValues = (a_slot: number) => {
  slotStyles.delete(a_slot);

  dirtySlots.delete(a_slot);

  const element = slotElements.get(a_slot);

  if (!element) {
    return;
  }

  element.style.removeProperty('transform');
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

  if (style) {
    applyStyle(a_element, style);
  }

  const health = slotHealth.get(a_slot);

  if (health) {
    applyHealth(a_element, health);
  }
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
 *
 * The position is the one the marker will have when the page is painted, so
 * it is written with two decimals: a marker that moves fast is put between
 * two pixels and the browser has to know where exactly.
 */
const applyStyle = (a_element: HTMLElement, a_style: MarkerStyle) => {
  const x = Math.round(Number(a_style.x) * 100) / 100;
  const y = Math.round(Number(a_style.y) * 100) / 100;

  const value = `translate(${x ?? -9999}px, ${y ?? -9999}px) scale(${a_style.scale ?? 1})`;
  a_element.style.setProperty('transform', value);
};

/**
 * Puts the health of an NPC into the styles of the element of its slot.
 *
 * The stylesheet turns the two values into the percentage of the bar, so
 * the view does not have to calculate anything on its own.
 */
const applyHealth = (a_element: HTMLElement, a_style: HealthStyle) => {
  a_element.style.setProperty('--npc-health-current', a_style.current);
  a_element.style.setProperty('--npc-health-max', a_style.max);
};
