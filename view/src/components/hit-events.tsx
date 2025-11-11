import { For, from } from 'solid-js';
import { HitEventBox } from './hit-event';
import { hitEvents$ } from '../plugin/plugin';
import { type Observable, delay, merge, mergeMap, of, scan } from 'rxjs';
import type { HitEvent } from '../plugin/types';

type Action =
  | { type: 'push'; item: HitEvent }
  | { type: 'remove'; item: HitEvent };

const actions$: Observable<Action> = hitEvents$.pipe(
  mergeMap((item) =>
    merge(
      of<Action>({ type: 'push', item }),
      of<Action>({ type: 'remove', item }).pipe(delay(1000))
    )
  )
);

const events$ = actions$.pipe(
  scan((list: HitEvent[], action: Action) => {
    switch (action.type) {
      case 'push':
        return [action.item, ...list];
      case 'remove':
        return list.filter((item) => item !== action.item);
      default:
        return list;
    }
  }, [] as HitEvent[])
);

export function HitEvents() {
  const events = from(events$);

  return (
    <div class="flex flex-col gap-3 text-white">
      <For each={events()}>{(event) => <HitEventBox event={event} />}</For>
    </div>
  );
}
