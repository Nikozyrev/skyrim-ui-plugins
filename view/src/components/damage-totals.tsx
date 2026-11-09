import { For, Show } from 'solid-js';
import { actors } from '../plugin/plugin';
import type { ActorStats } from '../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

const ActorName = (props: { stats: ActorStats }) => (
  <Show
    when={props.stats.isPlayer}
    fallback={<span class="truncate text-slate-200">{props.stats.name || 'Unknown'}</span>}
  >
    <span class="truncate font-semibold text-sky-300">{props.stats.name || 'You'}</span>
  </Show>
);

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
                <td class="text-right tabular-nums text-amber-300/90">
                  {formatAmount(stats.damageDealt)}
                </td>
                <td class="text-right tabular-nums text-rose-300/90">
                  {formatAmount(stats.damageTaken)}
                </td>
              </tr>
            )}
          </For>
        </tbody>
      </table>
    </div>
  );
}
