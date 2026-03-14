-- ============================================================
-- example_rotate.lua  --  tsuEngine Lua Script example
-- ============================================================
-- This script makes its entity rotate continuously on the Y axis.
-- Attach it via Add Component → Lua Script.
--
-- Available API  (all via the 'scene' global table):
--   scene.getPos(idx)             -> x, y, z
--   scene.setPos(idx, x, y, z)
--   scene.getRot(idx)             -> x, y, z  (Euler degrees)
--   scene.setRot(idx, x, y, z)
--   scene.getScale(idx)           -> x, y, z
--   scene.setScale(idx, x, y, z)
--   scene.getName(idx)            -> string
--   scene.findEntity(name)        -> idx  (-1 if not found)
--   scene.getEntityCount()        -> int
--   scene.getChannel(idx)         -> bool
--   scene.setChannel(idx, val)
--   scene.getPostEnabled()        -> bool
--   scene.setPostEnabled(bool)
--   scene.setExposure(float)
--   scene.setSaturation(float)
--   scene.setContrast(float)
--   scene.setBrightness(float)
--   scene.elapsed()               -> seconds since play started
--
-- 'entity_idx' is automatically set to this entity's 0-based index.
--
-- Expose variables to the inspector with --@expose:
--   --@expose <name> <type> <default>   (type = number | string | boolean)
-- NOTE: Do NOT declare exposed variables with 'local' — the engine sets them
--       as globals in your script environment before OnStart is called.
-- ============================================================

--@expose rotSpeed number 60.0
--@expose enabled boolean true

-- rotSpeed and enabled are set by the engine from the inspector (no 'local')

function OnStart()
    print("[Lua] OnStart  entity=" .. entity_idx
          .. "  name=" .. scene.getName(entity_idx))
end

function OnUpdate(dt)
    if not enabled then return end
    local rx, ry, rz = scene.getRot(entity_idx)
    local newY = ry + rotSpeed * dt
    scene.setRot(entity_idx, rx, newY, rz)
end

function OnStop()
    print("[Lua] OnStop  entity=" .. entity_idx)
end
