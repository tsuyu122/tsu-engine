# Lua API Reference

This page documents every function available to Lua scripts in TSU Engine, covering transforms, entity queries, input channels, post-processing, and time utilities.

## How the API Works

All bindings are registered into a global Lua table called **`scene`** by `LuaSystem::Init()` before `OnStart()` is ever called. Every function in this table maps directly to a C++ function in `engine/lua/luaSystem.cpp`.

Scripts run in **per-entity sandboxed environments** — each script gets its own environment table whose `__index` metatable falls back to `_G`. This means all Lua standard library globals (`math`, `string`, `table`, `print`, etc.) are available, while `entity_idx` and `--@expose` variables are injected per-entity without polluting other scripts.

---

## Quick Example Script

```lua
--@expose speed   number  3.0
--@expose channel number  0

function OnStart()
    print("Entity " .. scene.getName(entity_idx) .. " is ready.")
end

function OnUpdate(dt)
    -- Move forward only when channel 0 is true
    if scene.getChannel(channel) then
        local x, y, z = scene.getPos(entity_idx)
        scene.setPos(entity_idx, x + speed * dt, y, z)
    end
end

function OnStop()
    scene.setChannel(channel, false)
end
```

For more complete examples see [`docs/lua/examples.md`](examples.md).

---

## Injected Globals

| Global | Type | Description |
|---|---|---|
| `entity_idx` | `number` | 0-based index of the entity that owns this script |

---

## Function Index

| Function | Returns | Summary |
|---|---|---|
| `scene.getPos(idx)` | `x, y, z` | World position of entity |
| `scene.setPos(idx, x, y, z)` | — | Set local position |
| `scene.getRot(idx)` | `x, y, z` | Local Euler rotation (degrees) |
| `scene.setRot(idx, x, y, z)` | — | Set local rotation (degrees) |
| `scene.getScale(idx)` | `x, y, z` | Local scale |
| `scene.setScale(idx, x, y, z)` | — | Set local scale |
| `scene.getName(idx)` | `string` | Display name of entity |
| `scene.findEntity(name)` | `number` | Index of entity by name, or `-1` |
| `scene.getEntityCount()` | `number` | Total entity count in scene |
| `scene.getChannel(idx)` | `boolean` | Boolean value of input channel |
| `scene.setChannel(idx, value)` | — | Set boolean value of input channel |
| `scene.getChannelFloat(idx)` | `number` | Float value of input channel |
| `scene.setChannelFloat(idx, value)` | — | Set float value of input channel |
| `scene.getChannelString(idx)` | `string` | String value of input channel |
| `scene.setChannelString(idx, value)` | — | Set string value of input channel |
| `scene.spawnEntity(name)` | `number` | Spawn a new entity; returns its index |
| `scene.destroyEntity(idx)` | — | Destroy (tombstone) entity at runtime |
| `scene.isKeyDown(key)` | `boolean` | Keyboard state by key code or name |
| `scene.isMouseDown(button)` | `boolean` | Mouse button state (0=L 1=R 2=M) |
| `scene.getMouseDelta()` | `dx, dy` | Mouse movement delta this frame |
| `scene.getPostEnabled()` | `boolean` | Whether post-processing is on |
| `scene.setPostEnabled(enabled)` | — | Enable / disable post-processing |
| `scene.setExposure(value)` | — | Linear brightness multiplier |
| `scene.setSaturation(value)` | — | Color saturation (0 = grayscale) |
| `scene.setContrast(value)` | — | Midtone contrast |
| `scene.setBrightness(value)` | — | Additive brightness offset |
| `scene.elapsed()` | `number` | Seconds since Play began |
| `scene.getVelocity(idx)` | `vx, vy, vz` | Velocity of a rigid body |
| `scene.setVelocity(idx, vx, vy, vz)` | — | Override velocity of a rigid body |
| `scene.isGrounded(idx)` | `boolean` | Whether rigid body is on the ground |
| `scene.applyImpulse(idx, ix, iy, iz)` | — | Add an instant velocity change (impulse / mass) |
| `scene.setCursorLocked(locked)` | — | Lock or show the OS cursor |
| `scene.isCursorLocked()` | `boolean` | Whether the cursor is currently locked |
| `scene.loadScene(path)` | — | Load a new scene by filename (e.g. `"level2"`) |
| `scene.getDistance(a, b)` | `number` | World-space distance between two entities |
| `scene.getTag(idx)` | `string` | Tag string of entity |
| `scene.setTag(idx, tag)` | — | Set tag string of entity |
| `scene.findEntityByTag(tag)` | `number` | First entity with matching tag, or `-1` |
| `scene.isConnected()` | `boolean` | Whether the multiplayer session is active |
| `scene.isHost()` | `boolean` | Whether this instance is the host |
| `scene.playerCount()` | `number` | Number of connected players |
| `scene.getNetworkId(idx)` | `number` | Network ID of a multiplayer controller entity |
| `scene.setNickname(idx, name)` | — | Set nickname on a multiplayer controller entity |
| `scene.setAnimatorPlaying(idx, play)` | — | Start or stop a keyframe Animator |
| `scene.setAnimatorSpeed(idx, speed)` | — | Set playback speed multiplier of an Animator |
| `scene.setAnimatorBlend(idx, weight)` | — | Set blend weight (0..1) of an Animator |
| `scene.setAnimControllerState(idx, state)` | — | Force an Animation Controller into a specific state index |
| `scene.getAnimControllerState(idx)` | `number` | Current state index of an Animation Controller |
| `scene.setAnimControllerEnabled(idx, enable)` | — | Enable or disable an Animation Controller |
| `scene.setSSAO(enable, intensity, radius)` | — | Configure SSAO post-process |
| `scene.setDistanceCulling(enable, dist)` | — | Enable distance culling and set max draw distance |
| `scene.getLightmapIntensity()` | `number` | Current global lightmap intensity multiplier |
| `scene.setLightmapIntensity(value)` | — | Set global lightmap intensity multiplier |
| `scene.playAudio(idx)` | — | Trigger a one-shot play on an Audio Source entity |
| `scene.getAudioGain(idx)` | `number` | Current computed gain of an Audio Source entity |
| `scene.loadSkeleton(idx, path)` | `boolean` | Load a skeleton file onto a Skinned Mesh entity |
| `scene.setBonePose(idx, bone, tx,ty,tz, rx,ry,rz)` | — | Override a bone's local transform in a Skinned Mesh |

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

## scene — Entity Lifecycle

### `scene.spawnEntity(name)`
Spawns a new empty entity with the given name at runtime. Returns its index.

```lua
local bullet = scene.spawnEntity("Bullet")
scene.setPos(bullet, 0, 1, 0)
```

---

### `scene.destroyEntity(idx)`
Destroys entity `idx` at runtime. All its components are deactivated and the entity is tombstoned so it will not be found by `findEntity`. The index stays stable for the rest of the frame.

```lua
scene.destroyEntity(enemy)
```

---

## scene — Input

### `scene.isKeyDown(key)`
Returns `true` if the given key is currently held. `key` can be an **integer** GLFW key code or a **string** key name (`"W"`, `"Space"`, `"Escape"`, etc.).

```lua
if scene.isKeyDown("Space") then
    -- jump
end
```

---

### `scene.isMouseDown(button)`
Returns `true` if the given mouse button is held. `0` = left, `1` = right, `2` = middle.

```lua
if scene.isMouseDown(0) then
    -- left click held
end
```

---

### `scene.getMouseDelta()`
Returns the mouse movement delta `(dx, dy)` in pixels since the last frame. Useful for camera look scripts.

```lua
local dx, dy = scene.getMouseDelta()
local rx, ry, rz = scene.getRot(entity_idx)
scene.setRot(entity_idx, rx - dy * 0.1, ry - dx * 0.1, rz)
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

### `scene.getChannelFloat(idx)`
Returns the **float value** of channel `idx`.

```lua
local speed = scene.getChannelFloat(1)
```

---

### `scene.setChannelFloat(idx, value)`
Sets the **float value** of channel `idx`.

```lua
scene.setChannelFloat(1, 5.5)
```

---

### `scene.getChannelString(idx)`
Returns the **string value** of channel `idx`.

```lua
local label = scene.getChannelString(2)
```

---

### `scene.setChannelString(idx, value)`
Sets the **string value** of channel `idx`.

```lua
scene.setChannelString(2, "ready")
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

## scene — Physics

All physics functions operate on entities that have a **RigidBody** component. Calling them on entities without one has no effect.

### `scene.getVelocity(idx)`
Returns the current velocity of entity `idx` as three separate numbers `(vx, vy, vz)`.

```lua
local vx, vy, vz = scene.getVelocity(entity_idx)
```

---

### `scene.setVelocity(idx, vx, vy, vz)`
Directly overrides the velocity of entity `idx`.

```lua
scene.setVelocity(entity_idx, 0, 0, 0)   -- stop the body
```

---

### `scene.isGrounded(idx)`
Returns `true` if entity `idx` is currently flagged as grounded by the physics system.

```lua
if scene.isGrounded(entity_idx) then
    -- allow jumping
end
```

---

### `scene.applyImpulse(idx, ix, iy, iz)`
Applies an instant velocity change to entity `idx`. The impulse vector is divided by the body's mass internally, so the effect scales correctly regardless of weight.

```lua
-- jump
if scene.isGrounded(entity_idx) and scene.isKeyDown("Space") then
    scene.applyImpulse(entity_idx, 0, 8, 0)
end
```

---

## scene — Cursor

### `scene.setCursorLocked(locked)`
Locks or releases the OS cursor. When locked, the cursor is hidden and confined to the window — ideal for first-person camera control. Pass `false` to restore the cursor for menus.

```lua
function OnStart()
    scene.setCursorLocked(true)   -- hide cursor on game start
end

function OnStop()
    scene.setCursorLocked(false)  -- restore cursor when Play ends
end
```

---

### `scene.isCursorLocked()`
Returns `true` if the cursor is currently locked.

```lua
if not scene.isCursorLocked() then
    scene.setCursorLocked(true)
end
```

---

## scene — Scene Loading

### `scene.loadScene(path)`
Requests the engine to load a different scene on the next frame. `path` is the scene filename (with or without `.tscene`) relative to `assets/scenes/`.

The current script finishes its current `OnUpdate` call, then the engine stops all scripts, clears the scene, loads the new one, and starts scripts again.

```lua
-- Switch to level 2 when the player enters the trigger
function OnTriggerEnter(triggerIdx)
    scene.loadScene("level2")
end
```

```lua
-- You can also pass the full filename
scene.loadScene("level2.tscene")
```

> **Note:** `scene.loadScene` in the editor (Play mode) switches the current editing scene in addition to restarting scripts. In an exported game it only loads the new scene — the previous scene is not saved.

---

## Lifecycle Functions

These are **not** called by the script — they are **defined** by the script and called by the engine.

| Function | When called | Parameters |
|---|---|---|
| `OnStart()` | Once, when Play begins | none |
| `OnUpdate(dt)` | Every frame | `dt` — delta time in seconds |
| `OnStop()` | When Play ends | none |
| `OnTriggerEnter(triggerIdx)` | When a player enters a trigger volume | `triggerIdx` — entity index of the trigger |
| `OnTriggerExit(triggerIdx)` | When the player leaves a trigger volume | `triggerIdx` — entity index of the trigger |

All five are optional. Define only the ones your script needs.

### Trigger Callbacks

`OnTriggerEnter` and `OnTriggerExit` are broadcast to **every** script in the scene when any trigger volume fires. Use `triggerIdx` to filter the event to only the trigger you care about:

```lua
local myTrigger = -1

function OnStart()
    myTrigger = scene.findEntity("Door_Trigger")
end

function OnTriggerEnter(triggerIdx)
    if triggerIdx == myTrigger then
        print("Player entered the door trigger!")
    end
end

function OnTriggerExit(triggerIdx)
    if triggerIdx == myTrigger then
        print("Player left the door trigger.")
    end
end
```

---

---

## scene — Entity Tags

### `scene.getTag(idx)`
Returns the tag string of entity `idx`, or `""` if none set.

```lua
local tag = scene.getTag(entity_idx)
```

---

### `scene.setTag(idx, tag)`
Sets the tag string of entity `idx`. Tags are arbitrary strings used for grouping or filtering entities.

```lua
scene.setTag(entity_idx, "enemy")
```

---

### `scene.findEntityByTag(tag)`
Returns the index of the **first** entity whose tag matches `tag`, or `-1` if not found.

```lua
local boss = scene.findEntityByTag("boss")
if boss >= 0 then
    local x, y, z = scene.getPos(boss)
end
```

---

### `scene.getDistance(a, b)`
Returns the world-space distance between entity `a` and entity `b`.

```lua
local d = scene.getDistance(entity_idx, target)
if d < 3.0 then
    print("Close enough!")
end
```

---

## scene — Multiplayer

These functions query the active multiplayer session. They are safe to call even when no session is running (they return safe defaults).

### `scene.isConnected()`
Returns `true` if a multiplayer session is currently active (host is running or client is connected).

---

### `scene.isHost()`
Returns `true` if this instance is running as the host.

```lua
if scene.isHost() then
    -- Only the host runs authoritative logic
end
```

---

### `scene.playerCount()`
Returns the number of currently connected players (including the host).

```lua
print("Players online: " .. scene.playerCount())
```

---

### `scene.getNetworkId(idx)`
Returns the `uint64` Network ID of a multiplayer controller at entity `idx`. Returns `0` if the entity has no active controller.

---

### `scene.setNickname(idx, name)`
Sets the display nickname of the multiplayer controller at entity `idx`. Also computes a new Network ID from the name if the current one is `0`.

```lua
scene.setNickname(entity_idx, "Speedrunner42")
```

---

## scene — Animators

### `scene.setAnimatorPlaying(idx, play)`
Starts (`play = 1`) or stops (`play = 0`) the keyframe Animator on entity `idx`.

```lua
scene.setAnimatorPlaying(door_idx, 1)   -- play the open animation
```

---

### `scene.setAnimatorSpeed(idx, speed)`
Sets the playback speed multiplier of the Animator on entity `idx`. `1.0` = normal, `2.0` = double speed, `-1.0` = reverse.

---

### `scene.setAnimatorBlend(idx, weight)`
Sets the blend weight (`0.0`–`1.0`) of the Animator on entity `idx`.

---

### `scene.setAnimControllerState(idx, state)`
Forces the Animation Controller on entity `idx` into state index `state` immediately, resetting state time to `0`.

```lua
scene.setAnimControllerState(player_idx, 2)   -- jump to state 2
```

---

### `scene.getAnimControllerState(idx)`
Returns the current state index of the Animation Controller on entity `idx`, or `-1` if no controller.

---

### `scene.setAnimControllerEnabled(idx, enable)`
Enables (`enable = 1`) or disables (`enable = 0`) the Animation Controller on entity `idx`.

---

## scene — Post-Processing (extended)

### `scene.setSSAO(enable, intensity, radius)`
Configures the SSAO post-processing effect. `intensity` and `radius` are optional (defaults: `0.35`, `2.0`).

```lua
scene.setSSAO(1, 0.5, 1.5)   -- enable with custom settings
scene.setSSAO(0)              -- disable
```

---

### `scene.setDistanceCulling(enable, dist)`
Enables or disables distance-based draw call culling. `dist` is the max draw distance in world units (default: `150.0`). Useful for performance in large scenes.

```lua
scene.setDistanceCulling(1, 80.0)
```

---

### `scene.getLightmapIntensity()`
Returns the current global lightmap intensity multiplier.

---

### `scene.setLightmapIntensity(value)`
Sets the global lightmap intensity multiplier. Useful for day/night transitions driven from Lua.

```lua
scene.setLightmapIntensity(0.3)   -- dim the baked GI
```

---

## scene — Audio

### `scene.playAudio(idx)`
Triggers a one-shot playback on the Audio Source component of entity `idx`.

```lua
local sfx = scene.findEntity("Explosion_SFX")
scene.playAudio(sfx)
```

---

### `scene.getAudioGain(idx)`
Returns the current computed gain (volume, `0.0`–`1.0`) of the Audio Source at entity `idx`, factoring in distance attenuation and occlusion.

---

## scene — Skeletal Mesh

### `scene.loadSkeleton(idx, path)`
Loads a skeleton file from `path` onto the Skinned Mesh component of entity `idx`. Returns `true` on success.

```lua
scene.loadSkeleton(char_idx, "assets/meshes/hero.skeleton")
```

---

### `scene.setBonePose(idx, bone, tx, ty, tz, rx, ry, rz)`
Overrides the local transform of bone index `bone` on the Skinned Mesh at entity `idx`. Position in world units; rotation in degrees (Yaw/Pitch/Roll order).

```lua
-- Tilt bone 3 forward by 20 degrees
scene.setBonePose(char_idx, 3,  0,0,0,  20,0,0)
```

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
