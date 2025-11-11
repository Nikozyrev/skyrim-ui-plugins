// import { createStore } from 'solid-js/store';
import type { HitEvent } from './types';
import { Subject } from 'rxjs';

export const hitEvents$ = new Subject<HitEvent>();

// const pluginState = createStore({
//   hitEvents: [] as HitEvent[],
// });

// const setPluginState = pluginState[1];

const PluginHandlers = {
  onHitEvent: (targetName: string, source: number) =>
    hitEvents$.next({ targetName, source }),
  // setPluginState('hitEvents', (events) =>
  //   events.concat({ targetName, source })
  // ),
};

export const initPluginOnWindow = () => {
  Object.entries(PluginHandlers).forEach(
    ([key, value]) =>
      ((window as unknown as Record<string, unknown>)[key] = value)
  );
};

// export const usePluginState = () => pluginState;
