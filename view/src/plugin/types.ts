export type DamageKind = 'physical' | 'fire' | 'frost' | 'shock' | 'poison' | 'magic';

/** Accumulated damage per damage kind. Kinds without damage are left out. */
export type DamageKindBreakdown = Partial<Record<DamageKind, number>>;

export type DamageEvent = {
  /** Form ID of the attacker, "00000000" when the damage has no source. */
  attackerId: string;
  attackerName: string;
  attackerIsPlayer: boolean;

  targetId: string;
  targetName: string;
  targetIsPlayer: boolean;

  /** Applied damage, always positive. */
  damage: number;

  /** The elemental nature of the damage. Missing in very old plugin builds. */
  kind?: DamageKind;

  /** Milliseconds since epoch, used to age out old entries. */
  time: number;
};

/**
 * The damage a single target received, as the overlay keeps track of it.
 *
 * The overlay builds this map from the damage events, keyed by the form id of
 * the target, so it can look the damage of an NPC up by the id of its marker.
 */
export type TargetDamage = {
  /** The accumulated damage the target received. */
  amount: number;

  /** The kind of the most recent hit that damaged the target. */
  kind: DamageKind;
};

export type ActorStats = {
  id: string;
  name: string;
  isPlayer: boolean;
  damageDealt: number;
  damageTaken: number;
  hitsDealt: number;
  hitsTaken: number;

  /** Damage the actor dealt, broken down by kind. */
  dealtByKind?: DamageKindBreakdown;

  /** Damage the actor took, broken down by kind. */
  takenByKind?: DamageKindBreakdown;
};

export type DamageUpdate = {
  /** "event" carries a single new hit, "snapshot" carries the complete state. */
  type: 'event' | 'snapshot';

  /** Aggregated damage per character. */
  actors: ActorStats[];

  /** Only set for "event" updates. */
  event?: DamageEvent | null;

  /** Only set for "snapshot" updates. */
  events?: DamageEvent[] | null;
};

/**
 * The NPC of every occupied slot, keyed by the number of the slot.
 *
 * The plugin owns the slots: it hands a free one to every NPC that enters
 * the screen and takes it back when the NPC leaves. The id of an NPC travels
 * with the number of the slot that it occupies, so the view can look it up
 * without knowing it in advance.
 */
export type NpcSlots = Record<number, string>;

/**
 * The names of the NPCs, keyed by their form id.
 *
 * The names travel on a channel of their own, so they are kept in a store of
 * their own: a name never changes, so it is only sent when the view does not
 * know it yet.
 */
export type NpcNames = Record<string, string>;

/**
 * The health of a single NPC.
 */
export type NpcHealthEntry = {
  /** The health the NPC has right now. */
  current: number;

  /** The health the NPC can have at most. */
  max: number;
};

/**
 * The health of the NPCs, keyed by their form id.
 *
 * The health travels on a channel of its own: it is sent when the NPC
 * enters the screen and every time it takes damage, so the bar above its
 * head always shows the health of the NPC.
 */
export type NpcHealth = Record<string, NpcHealthEntry>;

/**
 * One NPC that entered the screen.
 *
 * The payload of the plugin is a frame of the tuples "slot:id", separated by
 * semicolons. The name of the NPC travels on the channel of the names.
 */
export type NpcSlotEntry = {
  /** The number of the slot of the overlay. */
  slot: number;

  /** The form id of the NPC. */
  id: string;
};

/**
 * The NPCs that entered the screen.
 *
 * The plugin only sends the NPCs that entered the screen, so the store of the
 * view is only touched when a NPC really shows up.
 */
export type NpcSlotsShow = NpcSlotEntry[];

/**
 * The numbers of the slots of the NPCs that left the screen.
 *
 * The plugin only sends the slots of the NPCs that left the screen, so the
 * store of the view is only touched when a NPC really goes away.
 */
export type NpcSlotsHide = number[];

/**
 * A frame of the positions of the markers.
 *
 * The frame is a plain string, so it is parsed by hand: it is the hottest
 * path of the overlay and a JSON parser would cost more than the frame
 * itself.
 *
 * The frame looks like this:
 *
 * ```
 * T:1753600000123;0:100.5,200.5,1.00;3:400.0,300.0,1.25
 * ```
 *
 * The first part is the time of the frame: the moment the plugin measured
 * the positions, in milliseconds since the epoch. It is the clock of the
 * system, so the view can compare it with its own clock and tell how old the
 * positions are.
 *
 * Every other part is a tuple "slot:x,y,scale": the number of the slot of
 * the overlay, the horizontal offset, the vertical offset and the scale of
 * the font of the number. The offsets are in pixels with a tenth of a pixel.
 *
 * The positions are already the future ones: the plugin moves every marker
 * forward by the length of the frame the view is behind, so the view only
 * writes the numbers into the element of the slot.
 *
 * The frames are joined when they arrive faster than the browser draws them,
 * so a payload can hold the times of several frames.
 */
export type NpcSlotsMove = string;

/**
 * The styles of the overlay.
 *
 * The plugin owns the styles of the numbers above the heads, so the view does
 * not have to know the base size of their font: the plugin sends it and the
 * view sets it as a global custom property of the document.
 */
export type StyleUpdate = {
  /** "style" carries the styles of the overlay. */
  type: 'style';

  /** The base size of the font of the numbers, in pixels. */
  baseFontSize: number;

  /** The number of the slots of the overlay. */
  slotCount?: number;
};
