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
 * The ids of the NPCs that are visible on the screen.
 *
 * The plugin only sends this payload when the set of the visible NPCs
 * changed, so the ids are the stable key of the list of markers: the view
 * can reuse the elements it already drew.
 *
 * The payload is a plain array of the form ids: the ids have their own
 * channel, so they do not need a type of their own.
 *
 * The order matters: the frames of the positions address their markers by
 * the index of the NPC in this list.
 */
export type NpcIdsUpdate = string[];

/**
 * The names of the NPCs that are visible on the screen.
 *
 * The plugin only sends the names of the NPCs that entered the screen, in
 * their own message, so the names never travel with the positions.
 *
 * The payload is a plain record of the display names, keyed by the form id of
 * their NPC: the names have their own channel, so they do not need a type of
 * their own.
 */
export type NpcNamesUpdate = Record<string, string>;

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
};
