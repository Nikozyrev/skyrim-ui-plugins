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

/** The number above the head of a single NPC that is visible on the screen. */
export type NpcMarker = {
  /** Form ID of the NPC. */
  id: string;

  /** The horizontal offset of the number, in pixels. */
  x: number;

  /** The vertical offset of the number, in pixels. */
  y: number;

  /** The scale of the font of the number. */
  scale: number;

  /**
   * True once the position of the marker was received.
   *
   * The plugin announces the ids of the visible NPCs before it sends their
   * positions, so a marker exists a moment before it can be drawn.
   */
  placed: boolean;
};

/**
 * The ids of the NPCs that are visible on the screen.
 *
 * The plugin only sends this payload when the set of the visible NPCs
 * changed, so the ids are the stable key of the list of markers: the view
 * can reuse the elements it already drew.
 */
export type NpcIdsUpdate = {
  /** "npcIds" carries the ids of the NPCs that are visible on the screen. */
  type: 'npcIds';

  /**
   * The form ids of the visible NPCs.
   *
   * The order matters: the frames of the positions address their markers by
   * the index of the NPC in this list.
   */
  ids: string[];
};

/**
 * The names of the NPCs that are visible on the screen.
 *
 * The plugin only sends the names of the NPCs that entered the screen, in
 * their own message, so the names never travel with the positions.
 */
export type NpcNamesUpdate = {
  /** "npcNames" carries the names of the NPCs that entered the screen. */
  type: 'npcNames';

  /** The display names of the NPCs, keyed by their form id. */
  names: Record<string, string>;
};

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
