/* @refresh reload */
import { render } from 'solid-js/web';
import './index.css';
import App from './App.tsx';
import { initPluginOnWindow } from './plugin/plugin.ts';

initPluginOnWindow();

const root = document.getElementById('root');

render(() => <App />, root!);
