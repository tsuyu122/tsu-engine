#pragma once
#include <string>

struct GLFWwindow;

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
//    scene.getChannelFloat(idx)    -> number
//    scene.setChannelFloat(idx, val)
//    scene.getChannelString(idx)   -> string
//    scene.setChannelString(idx, val)
//    scene.getVelocity(idx)        -> x, y, z
//    scene.setVelocity(idx, x, y, z)
//    scene.isGrounded(idx)         -> bool
//    scene.applyImpulse(idx, x, y, z)
//    scene.setCursorLocked(bool)
//    scene.isCursorLocked()        -> bool
//    scene.getPostEnabled()        -> bool
//    scene.setPostEnabled(bool)
//    scene.setExposure(float)
//    scene.setSaturation(float)
//    scene.setContrast(float)
//    scene.setBrightness(float)
//    scene.elapsed()               -> float (seconds since play start)
//    scene.spawnEntity([name])     -> idx
//    scene.destroyEntity(idx)
//    scene.getMouseDelta()         -> dx, dy
//    scene.isMouseDown([btn])      -> bool  (0=left 1=right 2=mid)
//    scene.isKeyDown(key)          -> bool  (int code or string e.g. "W")
//
//  Trigger callbacks (optional — define in any script to receive events):
//    function OnTriggerEnter(triggerIdx)  -- fired for ALL active scripts
//    function OnTriggerExit(triggerIdx)
// ================================================================

class LuaSystem
{
public:
    // Initialize a fresh Lua state; registers scene API bindings; loads
    // all active+enabled LuaScript files so their environments are ready.
    static void Init(Scene& scene);

    // Associate a GLFW window so Lua scripts can control cursor lock.
    // Call once after Init (or whenever the window handle is available).
    static void SetWindow(GLFWwindow* window);

    // Fire OnStart() for every active+enabled script.  Must be called after Init.
    static void StartScripts();

    // Fire OnUpdate(dt) for every active+enabled script.
    static void UpdateScripts(Scene& scene, float dt);

    // Broadcast OnTriggerEnter / OnTriggerExit to all active scripts.
    static void FireTriggerEnter(int triggerEntityIdx);
    static void FireTriggerExit (int triggerEntityIdx);

    // Called by scene.loadScene(path) inside a script; consumed by the Application after UpdateScripts.
    static void        RequestLoadScene(const std::string& path);
    static std::string GetPendingLoadScene();   // returns "" if none pending

    // Fire OnStop() for every script, then tear down the Lua state.
    static void Shutdown();

    // Evaluate an arbitrary Lua string.  Returns "" on success, error text otherwise.
    static std::string ExecString(const std::string& code);
};

} // namespace tsu
