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

/** The head of a single NPC that is visible on the screen. */
export type NpcMarker = {
  /** Form ID of the NPC. */
  id: string;

  /** Display name of the NPC. */
  name: string;

  /** Horizontal position, 0 is the left edge of the screen and 1 the right one. */
  x: number;

  /** Vertical position, 0 is the bottom edge of the screen and 1 the top one. */
  y: number;

  /** Radius of the head circle as a fraction of the height of the screen. */
  r: number;
};

export type NpcUpdate = {
  /** "npc" carries the heads that are currently visible on the screen. */
  type: 'npc';

  /** The heads of the visible NPCs. */
  markers: NpcMarker[];
};
