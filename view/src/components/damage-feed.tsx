import { For, Show } from 'solid-js';
import { feed } from '../plugin/plugin';
import type { DamageEvent } from '../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

const ActorName = (props: { name: string; isPlayer: boolean }) => (
  <Show when={props.isPlayer} fallback={<span class="truncate text-slate-200">{props.name || 'Unknown'}</span>}>
    <span class="truncate font-semibold text-sky-300">{props.name || 'You'}</span>
  </Show>
);

const DamageRow = (props: { event: DamageEvent }) => (
  <li class="damage-row-enter flex items-baseline gap-2 text-[13px] leading-5">
    <span class="w-12 shrink-0 text-right font-semibold tabular-nums text-amber-300">
      {formatAmount(props.event.damage)}
    </span>
    <ActorName name={props.event.attackerName} isPlayer={props.event.attackerIsPlayer} />
    <span class="shrink-0 text-slate-500">→</span>
    <ActorName name={props.event.targetName} isPlayer={props.event.targetIsPlayer} />
  </li>
);

export function DamageFeed() {
  return (
    <div class="flex flex-col gap-1">
      <For each={feed()}>{(event) => <DamageRow event={event} />}</For>
    </div>
  );
}
