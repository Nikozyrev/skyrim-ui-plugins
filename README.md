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

### NPC head markers

The plugin tracks every NPC that is visible on the screen and draws the damage
that NPC received right above its head.

```
Skyrim ──► RE::ProcessLists::ForAllActors ──► Npc::Tracker
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
* resolves the head: the head bone of the humanoid skeleton
  (`NPCHead[Head]`) inside the model of the actor,
* projects the head with `RE::NiCamera::WorldPtToScreenPt3` of the camera that
  renders the world (the world camera below the camera root of the player
  camera),
* measures the size of the head with the very same projection: a probe point
  one head radius above the head is projected as well, and the distance
  between both points is the radius of the head on the screen.

The tracker stays **idle while no world is loaded**: in the main menu the
scene graph of the world is torn down, so the camera and the actors are not
valid there. The view keeps showing the damage overlay in the main menu and
only the head markers are switched off.

The result is normalized to the viewport of the game, so the view can place the
numbers at any resolution:

| Field | Meaning                                                       |
| ----- | ------------------------------------------------------------- |
| `x`   | 0 is the left edge of the screen, 1 the right one             |
| `y`   | 0 is the bottom edge of the screen, 1 the top one              |
| `r`   | radius of the head as a fraction of the height of the screen   |

`Npc::Overlay` drives the tracker as a SKSE game thread task and refreshes the
markers at most every 16 ms, so at most 60 times per second. The overlay stays
idle until the view is up.

The positions are quantized to integers before they leave the plugin: they are
scaled by 10000 and shifted by 0.05 of the screen, so a quantized coordinate is
never negative. A float32 needs up to 19 characters as text, an integer four,
and a movement that is smaller than a fraction of a pixel does not change the
value at all.

### Damage above the heads

The id of a marker and the `targetId` of a damage event are the same form id in
the same `%08X` notation, so the view can look the damage of a marker up
directly.

The view keeps the damage per target id in a Solid store. Every incoming damage
event adds its amount to the entry of its target, and the kind of the most
recent hit colours the number. A snapshot rebuilds the store from the
aggregates of the plugin, so the damage is not counted twice and the overlay
starts from a correct state after a save was loaded. Because the store is
written point by point, a hit only redraws the number above that one head.

The markers arrive at up to 60 times per second, so the view renders them with a
keyed list: the ids are the key of the list, the elements are reused between the
frames and only their position is updated. The markers live in a Solid store
instead of a signal, so a frame only touches the markers that really moved and
only those are drawn again. The number itself is recreated with every hit,
which restarts the animation that makes it pop up.

The ids of the visible NPCs travel on their own channel and only when the set
of the visible NPCs changed, so the list of the view stays stable while the
NPCs only move around. Their names travel on a third channel and only for the
NPCs that entered the screen: a name never changes, so it is read from the
engine once per NPC and sent once per NPC as well.

### Plugin ↔ view bridge

The plugin talks to the view through four global JavaScript functions:

```js
window.onDamageUpdate(JSON.stringify(payload));
window.onNpcIdsUpdate(JSON.stringify(payload));
window.onNpcNamesUpdate(JSON.stringify(payload));
window.onNpcMarkersUpdate(frame);
```

The damage payloads are always a JSON string, so they can never be
interpreted as code. The frame of the head positions is a plain string of
numbers, which is even cheaper: it is parsed by hand and never by `JSON.parse`.

Four payload types exist:

```jsonc
// a single new hit
{ "type": "event", "actors": [ /* ... */ ], "event": { /* ... */ } }

// the complete state
{ "type": "snapshot", "actors": [ /* ... */ ], "events": [ /* ... */ ] }

// the ids of the NPCs that are visible on the screen
{ "type": "npcIds", "ids": [ "000ABCDE", "0000BEEF" ] }

// the names of the NPCs that entered the screen, keyed by their id
{ "type": "npcNames", "names": { "000ABCDE": "Bandit" } }
```

An `npcIds` payload replaces the ids of the previous update completely, so an
NPC that left the screen disappears without a separate "remove" message. It is
only sent when the set of the visible NPCs changed: the bridge keeps the ids
it sent the last time and compares them with the current ones. The order of
the list is part of the protocol, because a frame addresses its markers by the
index of the NPC in it.

An `npcNames` payload only carries the NPCs that the view does not know yet,
so the names of the NPCs that are already on the screen are never sent twice.
The names arrive before the ids, so a marker is never drawn without its name.

A frame carries the positions of the heads:

```
0:10423,20155,120;1:9877,20310,95
```

Every tuple is `index:x,y,r`, separated by semicolons. The index is the
position of the NPC in the list of the ids, the numbers are the quantized
positions of the head. Only the markers that really moved are part of a frame,
so a frame is empty - and not sent at all - while nothing moves.

The view registers `window.onViewReady` as well and calls it once its own
handler is installed. The plugin answers with a snapshot, which closes the race
between the view being created and the view script being evaluated. The NPC
channels only start after that handshake, because they need the view to know
the `onNpcIdsUpdate`, `onNpcNamesUpdate` and `onNpcMarkersUpdate` functions.

The view draws the damage number of a marker with a single CSS transform that
carries both the position and the offset above the head:

```css
transform: translate(960px, 540px) translate(-50%, -100%);
```

The font size of the number follows the size of the head on the screen, so it
stays readable for NPCs that are far away.

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

### The overlay only exists while a world is loaded

The view is created as soon as a world is loaded, and not before. Two reasons:

* in the main menu the world is torn down, so the camera and the actors of the
  world are not valid and the head markers would read freed memory;
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
    NpcTypes.h           head marker of a single NPC
    NpcTracker.h/.cpp    collects the visible NPC heads
    NpcOverlay.h/.cpp    game thread task that refreshes the markers
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
| No numbers are drawn above the NPCs            | The camera could not be resolved or the actors have no head. Check the log for `NPC overlay` messages. |
| The overlay stays empty after loading a save   | Expected: the statistics are reset when a save is loaded.              |

## Credits

* [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) for the game API.
* [PrismaUI](https://github.com/PrismaUI-SKSE/framework) for the UI framework.
* [Floating Damage NG](https://github.com/alandtse/FloatingDamageNG) for the
  reference implementation of the damage classification and its colour palette.
