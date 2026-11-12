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

The plugin calls four global functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcIdsUpdate(JSON.stringify(payload));
window.onNpcNamesUpdate(JSON.stringify(payload));
window.onNpcMarkersUpdate(frame);
```

The view announces itself through `window.onViewReady()`, which makes the plugin
push the complete state again. That closes the race between the view being
created and this script being evaluated.

The damage payloads are always a JSON string, so they can never be interpreted
as code. The frame of the head positions is a plain string of numbers.

| Payload type | Shape                                                                                     |
| ------------ | ----------------------------------------------------------------------------------------- |
| `event`      | `{ type: "event", actors: [...], event: {...} }` - a single new hit.                       |
| `snapshot`   | `{ type: "snapshot", actors: [...], events: [...] }` - the full state.                      |
| `npcIds`     | `{ type: "npcIds", ids: [...] }` - the NPCs that are visible on screen.                     |
| `npcNames`   | `{ type: "npcNames", names: { ... } }` - the names of the NPCs that entered the screen.    |

`actors` holds the aggregated damage per character, `events` the recent hits,
`ids` the visible NPCs and `names` their display names. See `src/plugin/types.ts`
for the exact shape.

A frame carries the positions of the heads and nothing else:

```
0:10423,20155,120;1:9877,20310,95
```

Every tuple is `index:x,y,r`, separated by semicolons. The index is the
position of the NPC in the list of the ids, the numbers are the quantized
positions of the head: they are scaled by 10000 and shifted by 0.05 of the
screen. Only the markers that really moved are part of a frame.

### Damage numbers above the heads

The view accumulates the damage per target id in `src/plugin/plugin.ts`: every
damage event adds its amount to the entry of its target, a snapshot rebuilds the
store from the aggregates of the plugin. The id of an NPC marker is the same id a
damage event reports as its target, so the number above a head is simply the
entry of the marker id.

The damage lives in a Solid store, so a hit only writes the entry of its target
and only the number above that head is drawn again.

The ids of the visible NPCs only change when an NPC enters or leaves the
screen, so the list of the markers is stable while the NPCs only move around.
The markers themselves arrive with every frame and only carry the position of
the head, so the elements are reused between the frames and only their position
and their number are updated.

The markers live in a Solid store, so a frame only writes the markers that
really moved and only those elements are drawn again. The names of the NPCs
arrive in their own message and are kept in a map of their own, so they never
travel with the positions.
