## Damage Overlay view

The PrismaUI view of the damage overlay plugin.

### Scripts

| Command           | Description                                                                 |
| ----------------- | --------------------------------------------------------------------------- |
| `npm run dev`     | Starts the Vite dev server on <http://localhost:5173>.                       |
| `npm run build`   | Type checks and builds the production bundle into `dist/`.                    |
| `npm run preview` | Serves the built bundle locally.                                             |
| `npm run deploy`  | Copies `dist/` into `<Skyrim>/PrismaUI/views/PrismaUI-Example-UI/`.          |

`npm run deploy` uses the `XSE_TES5_GAME_PATH` environment variable. A different
folder can be passed as an argument:

```bat
npm run deploy -- "D:\Games\Skyrim Special Edition"
```

### How the view talks to the plugin

The plugin calls two global functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcUpdate(JSON.stringify(payload));
```

The view announces itself through `window.onViewReady()`, which makes the plugin
push the complete state again. That closes the race between the view being
created and this script being evaluated.

The payload is always a JSON string, so it can never be interpreted as code.

| Payload type | Shape                                                                    |
| ------------ | ------------------------------------------------------------------------ |
| `event`      | `{ type: "event", actors: [...], event: {...} }` - a single new hit.      |
| `snapshot`   | `{ type: "snapshot", actors: [...], events: [...] }` - the full state.     |
| `npc`        | `{ type: "npc", markers: [...] }` - the NPCs that are visible on screen.  |

`actors` holds the aggregated damage per character, `events` the recent hits,
`markers` the visible NPCs with their head position. See `src/plugin/types.ts`
for the exact shape.

### Damage numbers above the heads

The view accumulates the damage per target id in `src/plugin/plugin.ts`: every
damage event adds its amount to the entry of its target, a snapshot rebuilds the
map from the aggregates of the plugin. The id of an NPC marker is the same id a
damage event reports as its target, so the number above a head is simply the
entry of the marker id.

The markers arrive at up to 60 times per second, so they are rendered with a
keyed list: the elements are reused between the frames and only their position
and their number are updated.
