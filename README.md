# Skyrim Damage Overlay

A SKSE plugin that records the damage that is applied to characters in Skyrim and
renders it as an overlay through a [PrismaUI](https://www.prismaui.dev/) view.

The overlay shows two things:

* **Recent hits** – a live feed of the last few damage events (5 second lifetime).
* **Totals** – the accumulated damage per character: how much each character
  dealt and how much each character took.

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

### Plugin ↔ view bridge

The plugin talks to the view through a single global JavaScript function:

```js
window.onDamageUpdate(JSON.stringify(payload));
```

The payload is always a JSON string, so it can never be interpreted as code.
Two payload types exist:

```jsonc
// a single new hit
{ "type": "event", "actors": [ /* ... */ ], "event": { /* ... */ } }

// the complete state
{ "type": "snapshot", "actors": [ /* ... */ ], "events": [ /* ... */ ] }
```

The view registers `window.onViewReady` as well and calls it once its own
handler is installed. The plugin answers with a snapshot, which closes the race
between the view being created and the view script being evaluated.

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

| What                | Where                                                                                   |
| ------------------- | --------------------------------------------------------------------------------------- |
| `PrismaUI-Example-Plugin.dll` | `<Skyrim>/Plugins/`                                                            |
| `view/dist/*`       | `<Skyrim>/PrismaUI/views/PrismaUI-Example-UI/`                                          |

The DLL is copied automatically by the CommonLibSSE-NG plugin rule when one of
these environment variables is set:

* Path to a Skyrim install folder: `XSE_TES5_GAME_PATH`
* Path to a Mod Manager mods folder: `XSE_TES5_MODS_PATH`

The view is deployed with the helper script:

```bat
cd view
npm run deploy                                  # uses %XSE_TES5_GAME_PATH%
npm run deploy -- "D:\Games\Skyrim Special Edition"
```

The script replaces the contents of `PrismaUI/views/PrismaUI-Example-UI/`, so no
stale assets from a previous build are left behind.

## Project layout

```
src/
  main.cpp               entry point and SKSE message handling
  pch.h                  precompiled header
  PrismaUI_API.h         PrismaUI modder API (self-contained copy)
  damage/
    DamageTypes.h        damage event and per actor statistics
    DamageTracker.h/.cpp thread safe aggregation
    DamageHook.h/.cpp    Actor::HandleHealthDamage hooks
  ui/
    PrismaBridge.h/.cpp  failure tolerant PrismaUI wrapper
  keyhandler/            optional input helper (not wired up)
view/
  src/
    App.tsx              overlay layout
    components/          live feed and totals table
    plugin/              plugin ↔ view bridge and state
  scripts/deploy.mjs     copies dist/ into the PrismaUI views folder
```

## Troubleshooting

| Symptom                                        | Cause                                                                 |
| ---------------------------------------------- | --------------------------------------------------------------------- |
| `Failed to register messaging listener for ...` | The sender was not the name of a loaded plugin. Use `"SKSE"`.        |
| Nothing shows up, the log says the view failed | `PrismaUI/views/PrismaUI-Example-UI/index.html` is missing. Redeploy the view. |
| The log says PrismaUI was not found            | `PrismaUI.dll` is not installed or not loaded before the game starts. |
| No damage is recorded                          | The hook could not be installed. Check `Documents/My Games/Skyrim Special Edition/SKSE/skse64.log` for `Damage hook` messages. |
| The overlay stays empty after loading a save   | Expected: the statistics are reset when a save is loaded.              |

## Credits

* [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) for the game API.
* [PrismaUI](https://github.com/PrismaUI-SKSE/framework) for the UI framework.
