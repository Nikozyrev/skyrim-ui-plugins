import { For } from 'solid-js';
import { DAMAGE_KINDS, DAMAGE_KIND_FILL, DAMAGE_KIND_LABEL } from '../plugin/damage-colors';

/**
 * The colour key of the overlay. It stays visible even when no damage has
 * been recorded yet, so the meaning of the colours is always at hand.
 */
export function DamageLegend() {
  return (
    <div class="flex flex-wrap items-center gap-x-2.5 gap-y-1 text-[10px] uppercase tracking-wide text-slate-500">
      <For each={DAMAGE_KINDS}>
        {(kind) => (
          <span class="flex items-center gap-1">
            <span class={`h-1.5 w-1.5 rounded-full ${DAMAGE_KIND_FILL[kind]}`} />
            {DAMAGE_KIND_LABEL[kind]}
          </span>
        )}
      </For>
    </div>
  );
}
