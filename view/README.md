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
| `style`      | `{ type: "style", baseFontSize: 11 }` - the styles of the overlay.                          |

`actors` holds the aggregated damage per character, `events` the recent hits,
`ids` the visible NPCs and `names` their display names. See `src/plugin/types.ts`
for the exact shape.

A frame carries the numbers above the heads and nothing else:

```
0:1042,215,1.18;1:987,230,1.00
```

Every tuple is `index:x,y,scale`, separated by semicolons. The index is the
position of the NPC in the list of the ids, the numbers are the plain CSS
values of the number: the horizontal offset, the vertical offset in whole
pixels and the scale of the font. The scale is written with two decimals. The
plugin computes them, so the view only copies them into the stylesheet. Only
the markers that really moved are part of a frame.

The plugin sends the positions in pixels, so the view reports the size of its
viewport to the plugin:

```
window.onViewportUpdate("1920,1080")
```

The report is sent when the view starts, whenever the viewport changes and
repeatedly until the plugin answers with a frame: PrismaUI binds the function
of the listener only when the page finished loading, so the first report can
be lost.

The plugin owns the styles of the numbers as well. It sends the base size of
their font with the handshake and the view puts it into the global custom
property `--npc-damage-base-font-size` of the document:

```ts
document.documentElement.style.setProperty('--npc-damage-base-font-size', '11');
```

The stylesheet picks the value up, so the base size is written down only once,
in the plugin.

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
The markers themselves arrive with every frame and only carry the values of
the number above the head, so the elements are reused between the frames and
only their position and their number are updated.

The markers live in a Solid store, so a frame only writes the markers that
really moved and only those elements are drawn again. The names of the NPCs
arrive in their own message and are kept in a map of their own, so they never
travel with the positions.
