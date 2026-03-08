#pragma once
#include <string>

namespace tsu {

class Scene;

// ================================================================
// LuaSystem  --  Lua 5.4 scripting for tsuEngine
//
//  Usage:
//    LuaSystem::Init(scene)     -- call when play begins; sets up state + loads scripts
//    LuaSystem::StartScripts()  -- calls OnStart() on all active scripts (after Init)
//    LuaSystem::UpdateScripts(scene, dt) -- call every game-loop tick
//    LuaSystem::Shutdown()      -- call when play ends; destroys Lua state
//
//  Script interface (Lua side):
//    entity_idx  -- 0-based index of the entity this script belongs to (automatic global)
//
//    function OnStart()       -- called once when play begins
//    function OnUpdate(dt)    -- called every frame with delta-time (seconds)
//    function OnStop()        -- called when play ends
//
//  Built-in scene API (all functions accessible from Lua scripts):
//    scene.getPos(idx)             -> x, y, z
//    scene.setPos(idx, x, y, z)
//    scene.getRot(idx)             -> x, y, z  (Euler degrees)
//    scene.setRot(idx, x, y, z)
//    scene.getScale(idx)           -> x, y, z
//    scene.setScale(idx, x, y, z)
//    scene.getName(idx)            -> string
//    scene.findEntity(name)        -> idx  (-1 if not found)
//    scene.getEntityCount()        -> int
//    scene.getChannel(idx)         -> bool
//    scene.setChannel(idx, val)
//    scene.getPostEnabled()        -> bool
//    scene.setPostEnabled(bool)
//    scene.setExposure(float)
//    scene.setSaturation(float)
//    scene.setContrast(float)
//    scene.setBrightness(float)
//    scene.elapsed()               -> float (seconds since play start)
// ================================================================

class LuaSystem
{
public:
    // Initialize a fresh Lua state; registers scene API bindings; loads
    // all active+enabled LuaScript files so their environments are ready.
    static void Init(Scene& scene);

    // Fire OnStart() for every active+enabled script.  Must be called after Init.
    static void StartScripts();

    // Fire OnUpdate(dt) for every active+enabled script.
    static void UpdateScripts(Scene& scene, float dt);

    // Fire OnStop() for every script, then tear down the Lua state.
    static void Shutdown();

    // Evaluate an arbitrary Lua string.  Returns "" on success, error text otherwise.
    static std::string ExecString(const std::string& code);
};

} // namespace tsu
