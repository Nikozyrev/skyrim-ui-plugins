import type { DamageKind } from './types';

/**
 * The damage kinds in the order they are shown in the legend.
 */
export const DAMAGE_KINDS: readonly DamageKind[] = [
  'physical',
  'fire',
  'frost',
  'shock',
  'poison',
  'magic',
];

/**
 * Text colour of the damage amount, per damage kind.
 *
 * The palette follows Floating Damage NG, so the overlay reads the same way as
 * the floating combat text: orange for fire, light blue for frost, violet for
 * shock, green for poison.
 */
export const DAMAGE_KIND_TEXT: Record<DamageKind, string> = {
  physical: 'text-amber-300',
  fire: 'text-[#ff9500]',
  frost: 'text-[#5ac8fa]',
  shock: 'text-[#af52de]',
  poison: 'text-[#4cd964]',
  magic: 'text-[#c7b8e8]',
};

/** Fill colour of the breakdown bars, per damage kind. */
export const DAMAGE_KIND_FILL: Record<DamageKind, string> = {
  physical: 'bg-amber-300',
  fire: 'bg-[#ff9500]',
  frost: 'bg-[#5ac8fa]',
  shock: 'bg-[#af52de]',
  poison: 'bg-[#4cd964]',
  magic: 'bg-[#c7b8e8]',
};

/** Accent colour of a feed row, per damage kind. */
export const DAMAGE_KIND_BORDER: Record<DamageKind, string> = {
  physical: 'border-amber-300/50',
  fire: 'border-[#ff9500]/60',
  frost: 'border-[#5ac8fa]/60',
  shock: 'border-[#af52de]/60',
  poison: 'border-[#4cd964]/60',
  magic: 'border-[#c7b8e8]/60',
};

/** Human readable name of a damage kind. */
export const DAMAGE_KIND_LABEL: Record<DamageKind, string> = {
  physical: 'Physical',
  fire: 'Fire',
  frost: 'Frost',
  shock: 'Shock',
  poison: 'Poison',
  magic: 'Magic',
};

/**
 * Normalizes the kind reported by the plugin. Unknown or missing kinds are
 * treated as physical damage, so an outdated plugin build still renders.
 */
export const resolveKind = (kind: string | undefined): DamageKind =>
  kind !== undefined && kind in DAMAGE_KIND_TEXT ? (kind as DamageKind) : 'physical';
