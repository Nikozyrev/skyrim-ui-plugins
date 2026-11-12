import { For, Show } from 'solid-js';
import { feed } from '../../plugin/plugin';
import {
  DAMAGE_KIND_BORDER,
  DAMAGE_KIND_LABEL,
  DAMAGE_KIND_TEXT,
  resolveKind,
} from '../../plugin/damage-colors';
import type { DamageEvent } from '../../plugin/types';

const formatAmount = (value: number) => Math.round(value).toString();

const ActorName = (props: { name: string; isPlayer: boolean }) => (
  <Show when={props.isPlayer} fallback={<span class="truncate text-slate-200">{props.name || 'Unknown'}</span>}>
    <span class="truncate font-semibold text-sky-300">{props.name || 'You'}</span>
  </Show>
);

const DamageRow = (props: { event: DamageEvent }) => {
  const kind = () => resolveKind(props.event.kind);

  return (
    <li
      class={`damage-row-enter flex items-baseline gap-2 border-l-2 pl-2 text-[13px] leading-5 ${DAMAGE_KIND_BORDER[kind()]}`}
      title={`${DAMAGE_KIND_LABEL[kind()]} damage`}
    >
      <span class={`w-12 shrink-0 text-right font-semibold tabular-nums ${DAMAGE_KIND_TEXT[kind()]}`}>
        {formatAmount(props.event.damage)}
      </span>
      <ActorName name={props.event.attackerName} isPlayer={props.event.attackerIsPlayer} />
      <span class="shrink-0 text-slate-500">→</span>
      <ActorName name={props.event.targetName} isPlayer={props.event.targetIsPlayer} />
      <Show when={kind() !== 'physical'}>
        <span
          class={`ml-auto shrink-0 text-[10px] font-semibold uppercase tracking-wide ${DAMAGE_KIND_TEXT[kind()]}`}
        >
          {DAMAGE_KIND_LABEL[kind()]}
        </span>
      </Show>
    </li>
  );
};

export function DamageFeed() {
  return (
    <div class="flex flex-col gap-1">
      <For each={feed()}>{(event) => <DamageRow event={event} />}</For>
    </div>
  );
}
