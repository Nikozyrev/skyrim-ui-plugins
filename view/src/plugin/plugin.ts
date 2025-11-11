import { createStore } from 'solid-js/store';

const pluginState = createStore({
  focusLabel: '',
});

const setPluginState = pluginState[1];

const PluginHandlers = {
  updateFocusLabel: (label: string) => setPluginState('focusLabel', label),
};

export const initPluginOnWindow = () => {
  Object.entries(PluginHandlers).forEach(
    ([key, value]) =>
      ((window as unknown as Record<string, unknown>)[key] = value)
  );
};

export const usePluginState = () => pluginState;
