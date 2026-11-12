import { Show } from 'solid-js';
import { DamageFeed } from './components/damage-feed';
import { DamageLegend } from './components/damage-legend';
import { DamageTotals } from './components/damage-totals';
import { NpcHeads } from './components/npc-heads';
import { actors, connected, feed } from './plugin/plugin';

const SectionTitle = (props: { children: string }) => (
  <h2 class="text-[11px] font-semibold uppercase tracking-wider text-slate-400">{props.children}</h2>
);

function App() {
  const isEmpty = () => feed().length === 0 && actors().length === 0;

  return (
    <>
      <NpcHeads />

      <div class="pointer-events-none absolute left-3 top-3 flex w-80 flex-col gap-3 rounded-lg bg-slate-950/60 p-3 text-white shadow-lg backdrop-blur-sm">
        <div class="flex items-center justify-between">
          <h1 class="text-sm font-semibold tracking-wide">Damage</h1>
          <span
            class={`h-1.5 w-1.5 rounded-full ${connected() ? 'bg-emerald-400' : 'bg-slate-600'}`}
            title={connected() ? 'Connected to the plugin' : 'Waiting for the plugin'}
          />
        </div>

        <Show
          when={!isEmpty()}
          fallback={<p class="text-[13px] text-slate-500">No damage recorded yet.</p>}
        >
          <Show when={feed().length > 0}>
            <section class="flex flex-col gap-1">
              <SectionTitle>Recent hits</SectionTitle>
              <DamageFeed />
            </section>
          </Show>

          <Show when={actors().length > 0}>
            <section class="flex flex-col gap-1">
              <SectionTitle>Totals</SectionTitle>
              <DamageTotals />
            </section>
          </Show>
        </Show>

        <DamageLegend />
      </div>
    </>
  );
}

export default App;
