# Skyrim Damage Overlay

A SKSE plugin that records the damage that is applied to characters in Skyrim and
renders it as an overlay through a [PrismaUI](https://www.prismaui.dev/) view.

The overlay shows three things:

* **Recent hits** – a live feed of the last few damage events (5 second lifetime).
* **Totals** – the accumulated damage per character: how much each character
  dealt and how much each character took.
* **NPC damage** – the damage every visible NPC received, drawn as a number above
  its head.

Every hit is coloured by the source of the damage, so fire, frost, shock and
poison hits are told apart at a glance.

Only damage that involves the player is tracked, which keeps the overlay quiet
in busy locations.

## How it works

```
Skyrim ──► Actor::HandleHealthDamage ──► Damage::Hook ──► Damage::Tracker
                                                                     │
                                                     UI::Bridge ◄────┘
                                                                  │
                                                          PrismaUI view
```

### Damage source

The plugin hooks `Actor::HandleHealthDamage`, the single point the engine funnels
**every** health damage application through: melee, ranged, magic, poisons, traps,
falls and script driven damage. It reports the true, post mitigation amount
together with the responsible attacker.

`HandleHealthDamage` is a virtual function, therefore both actor vtables are
patched separately:

| Class                | Covers             | Vtable slot      |
| -------------------- | ------------------ | ---------------- |
| `RE::Character`      | every NPC          | `0x104` (SE/AE), `0x106` (VR) |
| `RE::PlayerCharacter`| the player         | `0x104` (SE/AE), `0x106` (VR) |

The data is captured *before* the original function runs, because the original
may destroy the target (death) and a later access would be a use-after-free.

### Damage kind

`HandleHealthDamage` does not tell what kind of damage it is, so the plugin
hooks the point where the engine applies an effect: `ValueModifierEffect::
ModifyActorValue`. The health change of an effect happens *inside* that call,
so the effect that applies the damage is still known when the damage is
reported. The effect is remembered for the duration of the call
(`Damage::Classifier::Context`) and the damage inherits its element.

The element itself comes from the magic effect (MGEF): the engine stores the
resistance that the effect bypasses in `EffectSetting::data.resistVariable`.

| `resistVariable`   | Kind      |
| ------------------ | --------- |
| `kResistFire`      | `fire`    |
| `kResistFrost`     | `frost`   |
| `kResistShock`     | `shock`   |
| `kPoisonResist`    | `poison`  |
| anything else      | `magic`   |

Damage that is not applied by an effect (weapon hits, falls) stays `physical`.

`ModifyActorValue` is virtual as well, so every effect class that can carry
damage owns its own vtable and is patched separately (slot `0x20` on all of
them):

| Class                                    | Carries                            |
| ---------------------------------------- | ---------------------------------- |
| `RE::ValueModifierEffect`                | continuous damage over time        |
| `RE::DualValueModifierEffect`            | continuous damage over time        |
| `RE::PeakValueModifierEffect`            | peak value effects                 |
| `RE::AbsorbEffect`                       | absorbing damage                   |
| `RE::AccumulatingValueModifierEffect`    | accumulating effects               |
| `RE::TargetValueModifierEffect`          | instant damage (fireball, ...)     |
| `RE::ValueAndConditionsEffect`           | conditional effects                |

The effect hooks are optional: if they cannot be installed, the damage is
still tracked and simply reported as physical damage.

The colours of the overlay follow
[Floating Damage NG](https://github.com/alandtse/FloatingDamageNG), so both
read the same way:

| Kind      | Colour     |
| --------- | ---------- |
| `physical` | amber     |
| `fire`     | `#ff9500`  |
| `frost`    | `#5ac8fa`  |
| `shock`    | `#af52de`  |
| `poison`   | `#4cd964`  |
| `magic`    | `#c7b8e8`  |

### NPC markers

The plugin tracks every NPC that is visible on the screen and draws the damage
that NPC received right above it.

```
Skyrim ──► RE::ProcessLists::ForAllActors ──► Npc::Tracker
                                                      │
                                                  Npc::SlotPool
                                                      │
                                              UI::Bridge ◄──┘
                                                      │
                                                  PrismaUI view
```

`Npc::Tracker` runs on the game thread and walks the actors of the current
game session. For every actor it

* skips the player, creatures without an actor base, disabled actors, actors
  without a model and dead bodies (`kIncludeDeadNpcs` in `NpcTracker.cpp`
  turns the last one back on),
* resolves the height of the NPC: `RE::Actor::GetHeight()` of the engine, which
  caches it in the high process of the actor, and the height of the race of the
  NPC as the fallback. The world units of the game are the units of the
  creation kit, so a human is 128 units high (`kBaseBodyHeight` in
  `NpcTracker.cpp`). The height is measured once per NPC and then kept, so it
  never changes while the NPC is on the screen,
* builds the point above the NPC from the root of its model and that height, so
  a swing of a weapon or a crouch does not move the marker,
* projects that point with `RE::NiCamera::WorldPtToScreenPt3` of the camera that
  renders the world (the world camera below the camera root of the player
  camera),
* measures the size of the head with the very same projection: a probe point
  one head radius above the point is projected as well, and the distance
  between both points is the radius of the head on the screen. The size of the
  head itself is a part of the height of the NPC
  (`kHeadRadiusPerHeight` in `NpcTracker.cpp`).

The point of the marker does not depend on the model of the NPC at all. The
bound of a model follows every animation of the actor: it grows when the NPC
swings a weapon and it shrinks when it crouches, so a marker that is built
from it jumps around and can even disappear while the NPC fights.

Only the NPCs that are in sight are tracked: a ray is cast from the camera of
the player to the chest of the NPC, and the NPC is skipped when the ray hits
something else. A weapon or a shield of the NPC is a collidable of its own,
so a hit on it counts as a hit on the NPC itself.

The tracker stays **idle while no world is loaded**: in the main menu the
scene graph of the world is torn down, so the camera and the actors are not
valid there. The view keeps showing the damage overlay in the main menu and
only the NPC markers are switched off.

The tracker works with the viewport of the game normalized, because it does not
know the size of the window of the overlay. The bridge turns the result into
the plain CSS values of the number above the NPC and rounds them to whole
pixels:

| Field   | Meaning                                                       |
| ------- | ------------------------------------------------------------- |
| `x`     | the horizontal offset of the number, in pixels                 |
| `y`     | the vertical offset of the number, in pixels                   |
| `scale` | the scale of the font of the number                            |

The number is placed right above the tracked point, so the vertical offset is
shifted up by the radius of the head, and the size of the font follows that
radius: a number of an NPC that is far away stays small, but it never becomes
unreadable. The size is sent as the scale of the base size of the stylesheet,
so the view only copies these values into the stylesheet and does not compute
anything on its own.

`Npc::Overlay` drives the tracker from a hook of the frame update of the game and
refreshes the markers in every frame of the game, so at most 60 times per second
at 60 FPS and as often as the game draws at a higher rate. The overlay stays
idle until the view is up.

The hook replaces one call of the game with a call of the plugin. The plugin calls
the original function with the arguments of the call and gives its result back, so
the game does not notice anything. The four argument registers and the result are
forwarded, because the engine rebuilds the code between the versions and the
signature of the called function is not the same everywhere: an earlier version of
the hook dropped the first argument and the game crashed right after the call.

The plugin calls the original function first and measures the markers afterwards:
the camera of the world is only as fresh as the moment it is read, so a marker
that is projected with a camera of the last frame is one frame behind its NPC -
and that gap grows with the speed of the camera.

The place of the call is picked by the version of the game, because the engine
moves its code around and the offset of the call inside the function is not stable.
The id of the function is stable, so the function itself is taken from the address
library and only the offset is version dependent:

| Version      | Function of the address library | Offset of the call |
| ------------ | ------------------------------ | ------------------ |
| SE 1.5       | `35565`                        | `0x748`            |
| AE 1.6       | `36564`                        | `0xC26`            |
| AE 1.7.99+   | `36544` (calls `36564`)        | `0x160`            |

The view reports the size of its viewport to the plugin (`onViewportUpdate`),
because the positions are sent in pixels and the window of the overlay is not
the screen of the game. The plugin needs that size before it can send the
first frame, so nothing is sent before the view reported it. A movement that
is smaller than a tenth of a pixel does not change the value at all, so it does
not even show up in a frame.

### Frame synchronization

The overlay is drawn by a browser that runs on a thread of its own, so the
positions of the markers always reach the screen a little later than the frame
of the game they belong to. The delay is small, but it is a delay of the whole
picture: while the camera turns, every NPC moves over the screen, so a delay
of a single frame puts every marker a whole frame behind its NPC.

Three things keep the overlay on the NPC:

1. **The frame is measured with the camera of the frame that is drawn.** The
   plugin reads the camera right after the game updated the frame. The engine
   moves the camera either inside that update or after it, and the plugin
   notices which one it is: it reads the camera before and after the call and
   compares the states of the frames. When the camera turns out to be one
   frame old, one more frame is compensated, so the positions that go out are
   older than they look.

2. **The frame goes out first.** The positions of the markers are the payload
   that has to be on the screen as soon as possible, so they are sent before
   the names, the health and the slots that left the screen. The frame is
   small - a dozen bytes per marker - and it is the only thing that is sent in
   every frame.

3. **The plugin puts every marker where it will be when the game frame it
   belongs to is on the screen.** The page of PrismaUI is drawn in the frame
   after the one that was measured, and it is composited with the frame after
   that one, so a marker is a whole frame of the game behind its NPC. The
   plugin knows the length of a frame and how fast a marker moves, so it moves
   the marker forward by a whole frame. A camera that turns at a constant
   speed moves every point of the screen at a constant speed too, so the
   movement of a marker is a straight line and the compensation is exact
   while the camera turns.

   The whole calculation happens in the plugin: the view only writes the
   numbers it gets into the element of the slot. The speed of a marker is
   measured on every frame of the game, not only on the frames that are sent,
   so a marker that stops is noticed right away.

The prediction is the part that can be tuned: `kPredictionGain` in
`src/ui/PrismaBridge.cpp` is the share of the movement that is repeated. A
value of `0` turns the prediction off, a value of `1` repeats the whole
movement. The prediction also stops when the movement of a marker slows down,
so a camera that stops does not make the markers run away. The horizon of the
prediction is a whole number of frames of the game and never longer than
`kMaxPredictionMs`.

The state of the frame is written to the log of the plugin every 120 frames,
so the delay of the whole path, the price of the overlay and the horizon of
the prediction are visible:

```
NPC overlay: the positions of the last frame were 21 ms old, the game draws every 58 ms, the game updates the frame in 57 ms, the overlay takes 4 ms, the prediction looks 58 ms ahead (1 frame).
```

The view reports the delay, so the log holds the delay that the measurement of
the plugin needed until the page was painted. The update of the frame is the
time of the game itself, the overlay is the time that the overlay costs: the
collection of the NPCs, the projection of the markers and the push of the
positions. The horizon is the time that the markers were moved forward, in
whole frames of the game. When the camera of the frame turns out to be one
frame old, the line says so.


### Damage above the NPCs

The id of a marker and the `targetId` of a damage event are the same form id in
the same `%08X` notation, so the view can look the damage of a marker up
directly.

The view keeps the damage per target id in a Solid store. Every incoming damage
event adds its amount to the entry of its target, and the kind of the most
recent hit colours the number. A snapshot rebuilds the store from the
aggregates of the plugin, so the damage is not counted twice and the overlay
starts from a correct state after a save was loaded. Because the store is
written point by point, a hit only redraws the number above that one NPC.

The markers arrive at up to 60 times per second, so the view renders them with a
keyed list: the elements of the slots are created once and are never
re-created. The positions do not live in a store: every value of a marker is
written into the element of its own slot (`--npc-damage-x`,
`--npc-damage-y`, `--npc-damage-scale` and `--npc-damage-placed`). The
components register their elements in a map that is keyed by the number of
the slot, so the plugin finds the element of a slot by the number it already
knows. A frame therefore only writes the styles of the slots that really
moved, and the elements of the other slots are not touched at all. The number
itself is recreated with every hit, which restarts the animation that makes it
pop up.

### Slots

The slots of the overlay belong to the plugin. `Npc::SlotPool` owns them: it
keeps a stack of the free slots, a bit mask (`std::bitset`) of the slots that
are in use and a table of the NPCs that are on the screen, keyed by their form
id. Every frame the pool marks the NPCs that are still visible, frees the
slots of the NPCs that left the screen and hands a free slot to every NPC that
entered the screen. An NPC that does not get a slot is not shown at all.

Because the slots belong to the plugin, the view does not have to know how many
NPCs are on the screen: it draws one element per slot and follows the numbers
that the plugin sends. The id of an NPC travels with the number of its slot
and only when it enters the screen, so the view keeps it in a store that is
keyed by the number of the slot. The name of an NPC travels on a channel of
its own and is kept in a store of its own, keyed by the form id of the NPC.

### Plugin ↔ view bridge

The plugin talks to the view through five global JavaScript functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcSlotsHide(frame);
window.onNpcSlotsShow(frame);
window.onNpcSlotsMove(frame);
window.onNpcNames(JSON.stringify(names));
window.onStyleUpdate(JSON.stringify(payload));
```

The damage payloads are always a JSON string, so they can never be
interpreted as code. The frames of the NPC overlay are plain strings of
numbers, which are even cheaper: they are parsed by hand and never by
`JSON.parse`.

Six payload types exist:

```jsonc
// a single new hit
{ "type": "event", "actors": [ /* ... */ ], "event": { /* ... */ } }

// the complete state
{ "type": "snapshot", "actors": [ /* ... */ ], "events": [ /* ... */ ] }

// the names of the NPCs, keyed by their form id
{ "000ABCDE": "Bandit" }

// the styles of the overlay
{ "type": "style", "baseFontSize": 11, "slotCount": 50 }
```

An `npcSlotsHide` payload is a plain list of the numbers of the slots that
have to be freed, separated by semicolons:

```
3;7;12
```

The view drops the NPC of every slot of the list together with the values of
the slot.

An `npcSlotsShow` payload is a frame of the NPCs that entered the screen:

```
3:000ABCDE;7:0000BEEF
```

Every tuple is `slot:id`, separated by semicolons. The slot is the number of
the slot of the overlay and the id is the form id of the NPC. The payload
only carries the NPCs that entered the screen, so the id of an NPC is never
sent twice.

The names of the NPCs travel on a channel of their own: they are a record of
the form id of an NPC and its display name, and they are only sent when the
view does not know them yet. A name never changes, so it is read from the
engine once per NPC and sent once per NPC as well. The names arrive before
the frame of the slots, so a marker is never drawn without the name of its
NPC. When the world is replaced, the plugin sends `onNpcNamesClear` and the
view drops the names of the old world.

A `style` payload carries the styles of the overlay. It is sent with the
handshake and not with every frame: the base size of the font of the numbers
is a constant of the plugin, so the stylesheet does not have to know it. The
view sets the value as the global custom property
`--npc-damage-base-font-size` of the document, where the stylesheet picks it
up. The payload also carries the number of the slots, so the view knows how
many elements it has to draw.

A frame carries the numbers above the NPCs:

```
T:1753600000123;3:1042.5,215.4,1.18;7:987.0,230.9,1.00
```

Every tuple is `slot:x,y,scale`, separated by semicolons. The slot is the
number of the slot of the overlay, the numbers are the plain CSS values of the
number: the horizontal offset, the vertical offset and the scale of the font.
The offsets are written with a tenth of a pixel, so the plugin can tell how
fast a marker moves. The scale is written with two decimals, because it is a
whole font size divided by the base size of the stylesheet. Only the slots that
really moved are part of a frame, so a frame is empty - and not sent at all -
while nothing moves.

The frame starts with the time of the frame: `T:<milliseconds>`, the moment the
plugin measured the positions. The time is the clock of the system, so the view
compares it with its own clock and knows how old the positions are. The
positions themselves are already the future ones - the plugin moved every
marker forward by the length of the frame the view is behind - so the view only
writes them into the element of the slot. See
[Frame synchronization](#frame-synchronization).

The view registers `window.onViewReady` as well and calls it once its own
handler is installed. The plugin answers with a snapshot, which closes the race
between the view being created and the view script being evaluated. The NPC
channels only start after that handshake, because they need the view to know
the `onNpcSlotsHide`, `onNpcSlotsShow` and `onNpcSlotsMove` functions.

The view also reports the size of its viewport to the plugin, because the
positions are sent in pixels:

```
window.onViewportUpdate("1920,1080")
```

The plugin remembers that size and uses it for every conversion. The report is
repeated until the plugin answers with a frame, because PrismaUI binds the
function of the listener only when the page finished loading, so the first
report can be lost.

The view draws the damage number of a marker with a single CSS transform that
carries both the position and the offset above the NPC:

```css
transform: translate(960px, 540px) translate(-50%, -100%);
```

The size of the font is a scale of the base size, so the plugin sends the
scale with the offset. The base size itself comes from the plugin as well, so
it is written down only once:

```css
font-size: calc(var(--npc-damage-base-font-size) * 1px);
scale(var(--npc-damage-scale, 1));
```

### Failure tolerance

The plugin never takes the game down because of the UI:

* The PrismaUI API is acquired lazily and retried until PrismaUI is loaded.
* A missing PrismaUI only disables the overlay; damage tracking keeps working.
* Every payload is checked against the view validity before it is sent.
* No exception can unwind into the engine's damage routine.
* The view tolerates malformed payloads and ignores unknown ones.
* The tracker is bounded, so a long session cannot grow it without limits.

`InteropCall` hands the work over to PrismaUI's own render thread, so the plugin
may push updates from the game thread, from the DOM ready callback and from a
JavaScript listener callback alike. The bridge serializes the calls to keep the
payloads in order.

### A SKSE gotcha worth knowing

The sender of a SKSE message has to be the **name of a loaded plugin**. SKSE
resolves it with `LookupHandleFromName()`, which only knows `"SKSE"` and the
plugins that were loaded before the call:

```cpp
messaging->RegisterListener("SkyrimDamageOverlay", Handler);  // ✗ always fails
messaging->RegisterListener("SKSE", Handler);                  // ✓
```

A failing registration is silent apart from one error line in the log, and the
plugin then never receives `kPostLoad` - so nothing is ever initialized. Use
`"SKSE"` to receive the "all plugins have been loaded" message.

Because of that, the plugin does not depend on the messaging interface:

* the damage hook is installed directly in `SKSEPlugin_Load`;
* the view is brought up by a game thread poll (`SKSE::GetTaskInterface()`),
  which also covers PrismaUI being loaded after this plugin.

### `SKSE::TaskInterface` is not usable for per-frame work

The task interface of SKSE is **not** a way to run code every frame. A task is put
into a queue that the game empties on its own thread, and the tasks are executed
**synchronously**: a task that puts another task into the queue while it runs
grows the queue faster than the game can empty it. The queue fills up, the game
thread spends all of its time in the queue and the game hangs.

That is exactly what happened here: the overlay was driven by a task that re-armed
itself every frame. The queue was flooded, the game stopped reacting and the whole
session hung.

So: **do not use `SKSE::TaskInterface` for work that has to happen every frame.**
Use the hook of the frame update of the game for that (`Npc::Overlay::Install`).

The task interface is still fine for a one-off job: the poll that waits for
PrismaUI to be loaded puts a single task into the queue and that task puts the
next one into the queue, so at most one task is ever pending.

### The overlay only exists while a world is loaded

The view is created as soon as a world is loaded, and not before. Two reasons:

* in the main menu the world is torn down, so the camera and the actors of the
  world are not valid and the NPC markers would read freed memory;
* a PrismaUI view covers the whole screen. Over the main menu it swallows the
  input: the game keeps running, but the menu stops reacting to the mouse and
  the keyboard.

`GameState::IsWorldLoaded()` in `src/util/GameState.h` answers the question.
It is checked by the view creation and by the NPC tracker.

## Requirements

* [PrismaUI](https://www.prismaui.dev/) (and its prerequisites) installed.
* [SKSE](https://skse.silverlock.org/) for Skyrim Special Edition / Anniversary Edition.
* [XMake](https://xmake.io) 2.8.2+ and a C++23 compiler (MSVC, Clang-CL) to build the plugin.
* [Node.js](https://nodejs.org) to build the view.

## Building

### The plugin

```bat
xmake build
```

> This generates a `build/windows/` directory in the project's root directory with the build output.

### The view

```bat
cd view
npm install
npm run build
```

> This generates `view/dist/`. The built files use relative paths, so the folder can be copied anywhere.

## Deploying

The deploy script builds both the plugin and the view and copies them into an
MO2 mod folder:

| What                         | Where                                                            |
| ---------------------------- | ---------------------------------------------------------------- |
| `PrismaUI-Example-Plugin.dll` | `<mod>/SKSE/Plugins/`                                           |
| `view/dist/*`                | `<mod>/PrismaUI/views/PrismaUI-Example-UI/`                      |

The target folder is read from `SKYRIM_MOD_PATH` in the `.env` file in the
project root (copy `.env.example` to `.env` and adjust it):

```env
SKYRIM_MOD_PATH=D:\Games\Steam\steamapps\common\Skyrim Special Edition\MO2\mods\my-damage
```

Then run the script from the project root:

```bat
node deploy.mjs                                     # builds and deploys
node deploy.mjs --no-build                          # deploys the current artifacts
node deploy.mjs "D:\...\MO2\mods\my-damage"        # overrides the .env target
```

The script replaces the contents of `PrismaUI/views/PrismaUI-Example-UI/`, so no
stale assets from a previous build are left behind.

> The DLL is also copied automatically by the CommonLibSSE-NG plugin rule when
> `XSE_TES5_GAME_PATH` or `XSE_TES5_MODS_PATH` is set. The deploy script hides
> both variables from the build, so the mod folder above stays the only target.

## Project layout

```
src/
  main.cpp               entry point and SKSE message handling
  pch.h                  precompiled header
  PrismaUI_API.h         PrismaUI modder API (self-contained copy)
  damage/
    DamageTypes.h        damage event and per actor statistics
    DamageTypes.cpp      damage kind names
    DamageTracker.h/.cpp thread safe aggregation
    DamageHook.h/.cpp    Actor::HandleHealthDamage and effect hooks
    DamageClassifier.h/.cpp element of a damage application
  npc/
    NpcTypes.h           marker of a single NPC
    NpcTracker.h/.cpp    collects the visible NPC markers
    NpcSlots.h/.cpp      pool of the slots of the overlay
    NpcOverlay.h/.cpp    hook of the frame update that refreshes the markers
  ui/
    PrismaBridge.h/.cpp  failure tolerant PrismaUI wrapper
  util/
    ActorUtil.h          shared actor helpers (player check, display name)
  keyhandler/            optional input helper (not wired up)
view/
  src/
    App.tsx              overlay layout
    components/          live feed, totals table, colour legend, damage numbers
    plugin/              plugin ↔ view bridge, state and colour palette
deploy.mjs               builds and deploys the plugin and the view
.env                     deployment target (SKYRIM_MOD_PATH)
```

## Troubleshooting

| Symptom                                        | Cause                                                                 |
| ---------------------------------------------- | --------------------------------------------------------------------- |
| `Failed to register messaging listener for ...` | The sender was not the name of a loaded plugin. Use `"SKSE"`.        |
| Nothing shows up, the log says the view failed | `PrismaUI/views/PrismaUI-Example-UI/index.html` is missing. Redeploy the view. |
| The log says PrismaUI was not found            | `PrismaUI.dll` is not installed or not loaded before the game starts. |
| No damage is recorded                          | The hook could not be installed. Check `Documents/My Games/Skyrim Special Edition/SKSE/skse64.log` for `Damage hook` messages. |
| The overlay does not show up                   | The view is created only while a world is loaded. Check the log for `NPC tracker` and `PrismaUI view` messages. |
| No numbers are drawn above the NPCs            | The camera could not be resolved or the actors have no usable model bound. Check the log for `NPC overlay` messages. |
| The overlay stays empty after loading a save   | Expected: the statistics are reset when a save is loaded.              |

## Credits

* [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) for the game API.
* [PrismaUI](https://github.com/PrismaUI-SKSE/framework) for the UI framework.
* [Floating Damage NG](https://github.com/alandtse/FloatingDamageNG) for the
  reference implementation of the damage classification and its colour palette.
