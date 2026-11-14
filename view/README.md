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

The plugin calls five global functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcSlotsHide(frame);
window.onNpcSlotsShow(frame);
window.onNpcSlotsMove(frame);
window.onNpcNames(JSON.stringify(names));
window.onStyleUpdate(JSON.stringify(payload));
```

The view announces itself through `window.onViewReady()`, which makes the plugin
push the complete state again. That closes the race between the view being
created and this script being evaluated.

The damage payloads are always a JSON string, so they can never be interpreted
as code. The frames of the head overlay are plain strings of numbers.

| Payload type | Shape                                                                                     |
| ------------ | ----------------------------------------------------------------------------------------- |
| `event`      | `{ type: "event", actors: [...], event: {...} }` - a single new hit.                       |
| `snapshot`   | `{ type: "snapshot", actors: [...], events: [...] }` - the full state.                      |
| `npcSlots`   | a frame of the numbers of the slots of the NPCs that left the screen.                      |
| `npcShow`    | a frame of the NPCs that entered the screen, with the number of their slot.                |
| `npcNames`   | `{ ... }` - the names of the NPCs, keyed by their form id.                                 |
| `style`      | `{ type: "style", baseFontSize: 11, slotCount: 50 }` - the styles of the overlay.           |

`actors` holds the aggregated damage per character, `events` the recent hits.
See `src/plugin/types.ts` for the exact shape.

The slots of the overlay belong to the plugin: it hands a free one to every NPC
that enters the screen and takes it back when the NPC leaves. The view draws
one element per slot and follows the numbers that the plugin sends, so it never
has to know how many NPCs are on the screen.

The NPCs that left the screen travel as a plain list of the numbers of their
slots:

```
3;7;12
```

The view drops the NPC of every slot of the list together with the values of
the slot.

An NPC that entered the screen travels with the number of its slot and its id:

```
3:000ABCDE;7:0000BEEF
```

Every tuple is `slot:id`, separated by semicolons. The view keeps the id of the
NPC in a store that is keyed by the number of the slot, so it is never sent
twice.

The names travel on a channel of their own and are kept in a store of their
own, keyed by the form id of the NPC:

```json
{ "000ABCDE": "Bandit" }
```

A name never changes, so the plugin sends it only when the view does not know
it yet. The names arrive before the frame of the slots, so a marker is never
drawn without the name of its NPC.

A frame carries the numbers above the heads and nothing else:

```
3:1042,215,1.18;7:987,230,1.00
```

Every tuple is `slot:x,y,scale`, separated by semicolons. The slot is the
number of the slot of the overlay, the numbers are the plain CSS values of the
number: the horizontal offset, the vertical offset in whole pixels and the
scale of the font. The scale is written with two decimals. The plugin computes
them, so the view only copies them into the stylesheet. Only the slots that
really moved are part of a frame.

The view writes every value of a marker into the element of its own slot:

```ts
element.style.setProperty('--npc-damage-x', '1042');
```

The components register their elements in a map that is keyed by the number
of the slot, so the plugin finds the element of a slot by the number it
already knows and writes the values straight into it. The stylesheet resolves
the values, so a frame only touches the slots that really moved.

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

The slots of the overlay are stable while the NPCs only move around, so the
elements of the slots are reused between the frames and only their position and
their number are updated.

The positions do not live in a Solid store: every value of a marker is written
into the element of its own slot (`--npc-damage-x`, `--npc-damage-y`,
`--npc-damage-scale` and `--npc-damage-placed`). The components register their
elements in a map that is keyed by the number of the slot, so the plugin finds
the element of a slot by the number it already knows. A frame therefore only
writes the styles of the slots that really moved, and the elements of the other
slots are not touched at all. The id of an NPC arrives with the slot that it
occupies and is kept in a store of their own, and the name of an NPC arrives on
a channel of their own. Neither of them travels with the positions.
