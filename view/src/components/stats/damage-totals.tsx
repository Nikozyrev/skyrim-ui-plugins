import { For, Show } from 'solid-js';
import { actors } from '../../plugin/plugin';
import { DAMAGE_KINDS, DAMAGE_KIND_FILL } from '../../plugin/damage-colors';
import type { ActorStats, DamageKindBreakdown } from '../../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

const ActorName = (props: { stats: ActorStats }) => (
  <Show
    when={props.stats.isPlayer}
    fallback={<span class="truncate text-slate-200">{props.stats.name || 'Unknown'}</span>}
  >
    <span class="truncate font-semibold text-sky-300">{props.stats.name || 'You'}</span>
  </Show>
);

/**
 * A thin bar that shows how the damage of a character is spread over the
 * damage kinds. Every segment is coloured like the matching feed entry.
 */
const KindBar = (props: { breakdown: DamageKindBreakdown | undefined }) => {
  const entries = () =>
    DAMAGE_KINDS.map((kind) => ({ kind, value: props.breakdown?.[kind] ?? 0 })).filter(
      (entry) => entry.value > 0,
    );

  const total = () => entries().reduce((sum, entry) => sum + entry.value, 0);

  return (
    <div class="mt-0.5 flex h-1 w-full overflow-hidden rounded-full bg-slate-800/60">
      <For each={entries()}>
        {(entry) => (
          <div
            class={DAMAGE_KIND_FILL[entry.kind]}
            style={{ width: `${total() > 0 ? (entry.value / total()) * 100 : 0}%` }}
          />
        )}
      </For>
    </div>
  );
};

export function DamageTotals() {
  return (
    <div class="flex flex-col gap-1">
      <table class="w-full table-fixed text-[13px] leading-5">
        <thead>
          <tr class="text-left text-[11px] uppercase tracking-wide text-slate-400">
            <th class="w-1/2 truncate font-medium">Character</th>
            <th class="w-1/4 text-right font-medium">Dealt</th>
            <th class="w-1/4 text-right font-medium">Taken</th>
          </tr>
        </thead>
        <tbody>
          <For each={actors()}>
            {(stats) => (
              <tr>
                <td class="truncate">
                  <ActorName stats={stats} />
                </td>
                <td class="text-right tabular-nums text-slate-100">
                  <div>{formatAmount(stats.damageDealt)}</div>
                  <KindBar breakdown={stats.dealtByKind} />
                </td>
                <td class="text-right tabular-nums text-rose-300/90">
                  <div>{formatAmount(stats.damageTaken)}</div>
                  <KindBar breakdown={stats.takenByKind} />
                </td>
              </tr>
            )}
          </For>
        </tbody>
      </table>
    </div>
  );
}
