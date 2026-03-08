# Lua Scripting — Examples

Practical script examples for common game behaviors. All examples use the built-in `scene` API.

---

## 1. Basic Script Template

The minimum structure of a Lua script for TSU Engine:

```lua
function OnStart()
    -- runs once when Play begins
end

function OnUpdate(dt)
    -- runs every frame; dt = delta time in seconds
end

function OnStop()
    -- runs when Play ends
end
```

---

## 2. Rotating an Entity

Continuously rotates the entity around the Y axis at a configurable speed.

```lua
--@expose rotSpeed number 90.0     -- degrees per second
--@expose enabled  boolean true

function OnUpdate(dt)
    if not enabled then return end

    local rx, ry, rz = scene.getRot(entity_idx)
    scene.setRot(entity_idx, rx, ry + rotSpeed * dt, rz)
end
```

---

## 3. Moving an Entity Back and Forth

Uses `scene.elapsed()` to oscillate an entity along the X axis.

```lua
--@expose amplitude number 3.0    -- distance in units
--@expose frequency number 1.0    -- cycles per second

local startX = 0
local startY = 0
local startZ = 0

function OnStart()
    startX, startY, startZ = scene.getPos(entity_idx)
end

function OnUpdate(dt)
    local t = scene.elapsed()
    local offset = amplitude * math.sin(t * frequency * 2 * math.pi)
    scene.setPos(entity_idx, startX + offset, startY, startZ)
end
```

---

## 4. Activating a Door via Channel

Moves a door upward when input channel 0 becomes `true` (for example, triggered by a `TriggerVolume`).

```lua
--@expose openHeight number  4.0
--@expose speed      number  2.0

local baseY     = 0
local isOpen    = false
local currentY  = 0

function OnStart()
    local _, y, _ = scene.getPos(entity_idx)
    baseY    = y
    currentY = y
end

function OnUpdate(dt)
    local triggered = scene.getChannel(0)

    local targetY = triggered and (baseY + openHeight) or baseY

    -- smooth lerp toward target
    currentY = currentY + (targetY - currentY) * math.min(speed * dt, 1.0)
    local x, _, z = scene.getPos(entity_idx)
    scene.setPos(entity_idx, x, currentY, z)
end
```

---

## 5. Pulsing Post-Processing Exposure

Simulates a heartbeat or hit-flash effect by modifying the scene exposure.

```lua
--@expose pulseSpeed    number 3.0
--@expose pulseStrength number 0.3

function OnUpdate(dt)
    local t    = scene.elapsed()
    local wave = 1.0 + pulseStrength * math.abs(math.sin(t * pulseSpeed))
    scene.setExposure(wave)
end

function OnStop()
    scene.setExposure(1.0)   -- reset on stop
end
```

---

## 6. Finding and Moving Another Entity

Looks up an entity by name and moves it on `OnStart`.

```lua
function OnStart()
    local idx = scene.findEntity("Target_Cube")
    if idx >= 0 then
        scene.setPos(idx, 0, 5, 0)
    end
end
```

---

## 7. Scaling an Entity Over Time (OneShot Grow)

Grows the entity from a small size to full scale once when Play starts.

```lua
--@expose duration number 1.5

local timer   = 0.0
local done    = false

function OnStart()
    scene.setScale(entity_idx, 0.01, 0.01, 0.01)
end

function OnUpdate(dt)
    if done then return end

    timer = timer + dt
    local t = math.min(timer / duration, 1.0)

    -- EaseOut quadratic
    local e = 1.0 - (1.0 - t) * (1.0 - t)
    scene.setScale(entity_idx, e, e, e)

    if t >= 1.0 then
        done = true
    end
end
```

---

## 8. Toggling a Channel from a Script

A script that toggles channel 1 every time a fixed interval elapses — useful for timed events.

```lua
--@expose interval number 2.0    -- seconds between toggles

local timer = 0.0
local state = false

function OnUpdate(dt)
    timer = timer + dt
    if timer >= interval then
        timer = 0.0
        state = not state
        scene.setChannel(1, state)
    end
end
```

---

## 9. Printing Debug Info to Console

The standard Lua `print` function writes to the editor Console.

```lua
function OnStart()
    local x, y, z = scene.getPos(entity_idx)
    print(string.format("[%s] Start pos: %.2f, %.2f, %.2f",
          scene.getName(entity_idx), x, y, z))
end
```

---

## Tips

- Declared exposed variables **must be globals** (`speed = 5.0`, not `local speed = 5.0`). The engine injects them by name before `OnStart`.
- `entity_idx` is always available and refers to the entity that owns the script.
- All errors are logged to the Console — check there if a script is not behaving as expected.
- Scripts are reloaded from disk every time you press **Play**, so edits take effect immediately without restarting the engine.
