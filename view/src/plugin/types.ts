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
