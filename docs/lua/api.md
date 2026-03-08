# Lua API Reference

All functions listed here are accessible from any Lua script via the `scene` global table and other automatically injected globals.

---

## Injected Globals

| Global | Type | Description |
|---|---|---|
| `entity_idx` | `number` | 0-based index of the entity that owns this script |

---

## scene — Transform

### `scene.getPos(idx)`
Returns the **world position** of entity `idx` as three separate numbers.

```lua
local x, y, z = scene.getPos(entity_idx)
```

---

### `scene.setPos(idx, x, y, z)`
Sets the **local position** of entity `idx`.

```lua
scene.setPos(entity_idx, 0, 1, 0)
```

> Note: The position stored internally is the local position relative to the entity's parent. `getPos` returns the world position computed from the full parent chain.

---

### `scene.getRot(idx)`
Returns the **local rotation** of entity `idx` as Euler angles in **degrees** `(x, y, z)`.

```lua
local rx, ry, rz = scene.getRot(entity_idx)
```

---

### `scene.setRot(idx, x, y, z)`
Sets the **local rotation** of entity `idx` in degrees.

```lua
scene.setRot(entity_idx, 0, 90, 0)   -- face 90 degrees on Y axis
```

---

### `scene.getScale(idx)`
Returns the local scale of entity `idx` as `(x, y, z)`.

```lua
local sx, sy, sz = scene.getScale(entity_idx)
```

---

### `scene.setScale(idx, x, y, z)`
Sets the local scale of entity `idx`.

```lua
scene.setScale(entity_idx, 2, 2, 2)
```

---

## scene — Entity Queries

### `scene.getName(idx)`
Returns the display name of entity `idx` as a string.

```lua
local name = scene.getName(entity_idx)
print(name)
```

---

### `scene.findEntity(name)`
Searches for an entity by display name. Returns its index, or `-1` if not found.

```lua
local door = scene.findEntity("Door_01")
if door >= 0 then
    scene.setPos(door, 0, 5, 0)   -- lift the door
end
```

---

### `scene.getEntityCount()`
Returns the total number of entities in the scene.

```lua
local n = scene.getEntityCount()
```

---

## scene — Input Channels

Channels are named runtime variables stored in `Scene::Channels`. They allow scripts to read and write shared state without hard-coding key names.

### `scene.getChannel(idx)`
Returns the **boolean value** of channel `idx`.

```lua
local open = scene.getChannel(0)
```

---

### `scene.setChannel(idx, value)`
Sets the **boolean value** of channel `idx`.

```lua
scene.setChannel(0, true)
```

---

## scene — Post-Processing

These functions control the scene-wide post-processing effects at runtime.

### `scene.getPostEnabled()`
Returns `true` if the post-processing pass is currently enabled.

```lua
local postOn = scene.getPostEnabled()
```

---

### `scene.setPostEnabled(enabled)`
Enables or disables the post-processing pass.

```lua
scene.setPostEnabled(true)
```

---

### `scene.setExposure(value)`
Sets the linear brightness multiplier. Default is `1.0`.

```lua
scene.setExposure(1.5)   -- brighter
```

---

### `scene.setSaturation(value)`
Sets the color saturation. `0.0` = full grayscale, `1.0` = normal, above `1.0` = more vivid.

```lua
scene.setSaturation(0.0)   -- grayscale effect
```

---

### `scene.setContrast(value)`
Sets the midtone contrast. Default is `1.0`.

```lua
scene.setContrast(1.3)
```

---

### `scene.setBrightness(value)`
Adds an additive brightness offset. Default is `0.0`.

```lua
scene.setBrightness(-0.1)   -- slightly darker
```

---

## scene — Time

### `scene.elapsed()`
Returns the number of seconds elapsed since Play began (as a float).

```lua
local t = scene.elapsed()
local ping = math.sin(t * 2.0)   -- oscillates over time
```

---

## Lifecycle Functions

These are **not** called by the script — they are **defined** by the script and called by the engine.

| Function | When called | Parameters |
|---|---|---|
| `OnStart()` | Once, when Play begins | none |
| `OnUpdate(dt)` | Every frame | `dt` — delta time in seconds |
| `OnStop()` | When Play ends | none |

All three are optional. Define only the ones your script needs.

---

## Expose Annotations (editor only)

Variables annotated with `--@expose` are editable from the Inspector and injected as Lua globals before `OnStart()` is called.

```
--@expose variableName type defaultValue
```

| Type keyword | Lua type | Inspector widget |
|---|---|---|
| `number` | float | DragFloat |
| `boolean` | boolean | Checkbox |
| `string` | string | InputText |

The `defaultValue` is used only when the variable has never been set in the Inspector. Once set, the editor-stored value takes precedence.
