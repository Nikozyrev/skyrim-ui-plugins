import { usePluginState } from './plugin/plugin';

function App() {
  const [state] = usePluginState();

  return (
    <div class='flex flex-col gap-3 text-white'>
      <div>MY PLUGIN</div>
      <div class="text-lg p-4">{state.focusLabel}</div>
    </div>
  );
}

export default App;
