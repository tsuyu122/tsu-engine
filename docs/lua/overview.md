# Lua Scripting — Overview

TSU Engine embeds Lua 5.4 as the scripting language for gameplay behavior. Every entity in the scene can have a Lua script attached to it via the `LuaScriptComponent`. Scripts are reloaded on each Play session.

---

## How It Works

The engine uses a single shared Lua state per play session. Each entity with an active `LuaScriptComponent` gets its own **sandboxed environment** — a Lua table that acts as a private `_ENV` for that script. This means two entities can load the same script file without sharing variables.

### Lifecycle

| Phase | Engine call | Lua function |
|---|---|---|
| Play begins | `LuaSystem::Init` + `StartScripts` | `OnStart()` |
| Each frame | `LuaSystem::UpdateScripts(dt)` | `OnUpdate(dt)` |
| Play ends | `LuaSystem::Shutdown` | `OnStop()` |

All three lifecycle functions are optional. If a script does not define `OnStart`, the engine skips it silently.

---

## Attaching a Script

### From the Editor

1. Select an entity in the **Hierarchy** panel
2. In the **Inspector**, scroll to the **Lua Script** section
3. Drag a `.lua` file from the **Asset Browser** onto the script path field, or type the path manually
4. An automatic sync reads any `--@expose` annotations from the file

### From the Asset Browser

1. Right-click in the Asset Browser → **New Script** to create a starter `.lua` file
2. Drag the new file from the Asset Browser onto a script slot in the Inspector

---

## Script Environment

Each script runs inside a dedicated Lua environment. The following globals are automatically available inside every script:

| Global | Type | Description |
|---|---|---|
| `entity_idx` | `number` | 0-based index of the owning entity in the scene arrays |
| `scene` | `table` | Built-in scene API (see [api.md](api.md)) |

All other locals and globals you define are private to that entity's environment.

---

## Exposing Variables to the Inspector

The `--@expose` annotation lets you mark variables as editable from the Inspector without recompiling.

### Syntax

```lua
--@expose variableName type defaultValue
```

Supported types:

| Type | Inspector widget |
|---|---|
| `number` | Drag float field |
| `boolean` | Checkbox |
| `string` | Text input |

### How It Works

1. The engine parses the script file looking for `--@expose` lines
2. For each annotation, it creates an entry in the entity's `ExposedVars` map
3. Before `OnStart()` is called, the engine injects all exposed values as Lua globals into the entity's environment
4. You can edit the values from the Inspector at any time while in the editor

### Example

```lua
--@expose rotSpeed number 60.0
--@expose enabled boolean true

function OnUpdate(dt)
    if not enabled then return end
    local x, y, z = scene.getRot(entity_idx)
    scene.setRot(entity_idx, x, y + rotSpeed * dt, z)
end
```

> Do **not** declare exposed variables with `local`. They must be globals so the engine can inject them by name.

---

## Script Assets

Script files can be registered in the scene's **Script Assets** list (the Asset Browser shows them under a dedicated Lua section). Registered scripts show a green `LUA` icon and can be dragged onto any entity's script slot.

---

## Error Handling

If a script fails to load or a lifecycle function throws a Lua error, the engine prints the error to the editor **Console** and continues running. Errors in one entity's script do not stop other scripts from updating.

---

## Standalone Export

When exporting a game with **File → Export Game**, the `GameRuntime.exe` binary includes the Lua 5.4 runtime. All `.lua` files in `assets/scripts/` are copied alongside the executable, and the runtime loads them with the same lifecycle as the editor.
