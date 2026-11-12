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

The plugin calls three global functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcIdsUpdate(JSON.stringify(payload));
window.onNpcMarkersUpdate(JSON.stringify(payload));
```

The view announces itself through `window.onViewReady()`, which makes the plugin
push the complete state again. That closes the race between the view being
created and this script being evaluated.

The payload is always a JSON string, so it can never be interpreted as code.

| Payload type | Shape                                                                                     |
| ------------ | ----------------------------------------------------------------------------------------- |
| `event`      | `{ type: "event", actors: [...], event: {...} }` - a single new hit.                       |
| `snapshot`   | `{ type: "snapshot", actors: [...], events: [...] }` - the full state.                      |
| `npcIds`     | `{ type: "npcIds", ids: [...] }` - the NPCs that are visible on screen.                     |
| `npcMarkers` | `{ type: "npcMarkers", markers: { ... } }` - the heads of the visible NPCs, keyed by id.    |

`actors` holds the aggregated damage per character, `events` the recent hits,
`ids` the visible NPCs and `markers` their head position. See
`src/plugin/types.ts` for the exact shape.

### Damage numbers above the heads

The view accumulates the damage per target id in `src/plugin/plugin.ts`: every
damage event adds its amount to the entry of its target, a snapshot rebuilds the
map from the aggregates of the plugin. The id of an NPC marker is the same id a
damage event reports as its target, so the number above a head is simply the
entry of the marker id.

The ids of the visible NPCs only change when an NPC enters or leaves the
screen, so the list of the markers is stable while the NPCs only move around.
The markers themselves arrive with every frame and only carry the position of
the head, so the elements are reused between the frames and only their position
and their number are updated.
