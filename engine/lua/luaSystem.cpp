#include "lua/luaSystem.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "input/inputmanager.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <unordered_map>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace tsu {

// ----------------------------------------------------------------
// Internal state
// ----------------------------------------------------------------

static lua_State* gL         = nullptr;
static Scene*     gScene     = nullptr;
static double     gStartTime = 0.0;

// Per-entity Lua environment data
struct EntityScriptState
{
    int  envRef  = LUA_NOREF; // registry ref to the per-entity env table
    bool hasStart  = false;
    bool hasUpdate = false;
    bool hasStop   = false;
};
static std::unordered_map<int, EntityScriptState> gStates; // keyed by entity index

// ----------------------------------------------------------------
// Scene API bindings (C functions exposed to Lua as `scene.*`)
// ----------------------------------------------------------------

static void luaPushError(lua_State* L, const char* msg)
{
    (void)L; (void)msg;  // errors are silently reported; could be extended
}

#define CHECK_ENTITY(L, idx) \
    if (!gScene || idx < 0 || idx >= (int)gScene->Transforms.size()) \
    { lua_pushnil(L); return 1; }

static int lua_getPos(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    CHECK_ENTITY(L, idx)
    auto& t = gScene->Transforms[idx];
    lua_pushnumber(L, t.Position.x);
    lua_pushnumber(L, t.Position.y);
    lua_pushnumber(L, t.Position.z);
    return 3;
}

static int lua_setPos(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float x   = (float)luaL_checknumber(L, 2);
    float y   = (float)luaL_checknumber(L, 3);
    float z   = (float)luaL_checknumber(L, 4);
    CHECK_ENTITY(L, idx)
    gScene->Transforms[idx].Position = { x, y, z };
    return 0;
}

static int lua_getRot(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    CHECK_ENTITY(L, idx)
    auto& t = gScene->Transforms[idx];
    lua_pushnumber(L, t.Rotation.x);
    lua_pushnumber(L, t.Rotation.y);
    lua_pushnumber(L, t.Rotation.z);
    return 3;
}

static int lua_setRot(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float x   = (float)luaL_checknumber(L, 2);
    float y   = (float)luaL_checknumber(L, 3);
    float z   = (float)luaL_checknumber(L, 4);
    CHECK_ENTITY(L, idx)
    gScene->Transforms[idx].Rotation = { x, y, z };
    return 0;
}

static int lua_getScale(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    CHECK_ENTITY(L, idx)
    auto& t = gScene->Transforms[idx];
    lua_pushnumber(L, t.Scale.x);
    lua_pushnumber(L, t.Scale.y);
    lua_pushnumber(L, t.Scale.z);
    return 3;
}

static int lua_setScale(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float x   = (float)luaL_checknumber(L, 2);
    float y   = (float)luaL_checknumber(L, 3);
    float z   = (float)luaL_checknumber(L, 4);
    CHECK_ENTITY(L, idx)
    gScene->Transforms[idx].Scale = { x, y, z };
    return 0;
}

static int lua_getName(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->EntityNames.size())
    { lua_pushstring(L, ""); return 1; }
    lua_pushstring(L, gScene->EntityNames[idx].c_str());
    return 1;
}

static int lua_findEntity(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    if (!gScene) { lua_pushinteger(L, -1); return 1; }
    for (int i = 0; i < (int)gScene->EntityNames.size(); ++i)
        if (gScene->EntityNames[i] == name) { lua_pushinteger(L, i); return 1; }
    lua_pushinteger(L, -1);
    return 1;
}

static int lua_getEntityCount(lua_State* L)
{
    lua_pushinteger(L, gScene ? (int)gScene->Transforms.size() : 0);
    return 1;
}

static int lua_getChannel(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->Channels.size())
    { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, gScene->Channels[idx].BoolValue ? 1 : 0);
    return 1;
}

static int lua_setChannel(lua_State* L)
{
    int  idx = (int)luaL_checkinteger(L, 1);
    bool val = lua_toboolean(L, 2) != 0;
    if (gScene && idx >= 0 && idx < (int)gScene->Channels.size())
        gScene->Channels[idx].BoolValue = val;
    return 0;
}

static int lua_getPostEnabled(lua_State* L)
{
    lua_pushboolean(L, gScene && gScene->PostProcess.Enabled ? 1 : 0);
    return 1;
}

static int lua_setPostEnabled(lua_State* L)
{
    bool v = lua_toboolean(L, 1) != 0;
    if (gScene) gScene->PostProcess.Enabled = v;
    return 0;
}

static int lua_setExposure(lua_State* L)
{
    if (gScene) gScene->PostProcess.Exposure = (float)luaL_checknumber(L, 1);
    return 0;
}

static int lua_setSaturation(lua_State* L)
{
    if (gScene) gScene->PostProcess.Saturation = (float)luaL_checknumber(L, 1);
    return 0;
}

static int lua_setContrast(lua_State* L)
{
    if (gScene) gScene->PostProcess.Contrast = (float)luaL_checknumber(L, 1);
    return 0;
}

static int lua_setBrightness(lua_State* L)
{
    if (gScene) gScene->PostProcess.Brightness = (float)luaL_checknumber(L, 1);
    return 0;
}

static int lua_elapsed(lua_State* L)
{
    using namespace std::chrono;
    auto now = duration<double>(steady_clock::now().time_since_epoch()).count();
    lua_pushnumber(L, now - gStartTime);
    return 1;
}

// ----------------------------------------------------------------
// Entity spawn / destroy
// ----------------------------------------------------------------

static int lua_spawnEntity(lua_State* L)
{
    const char* name = luaL_optstring(L, 1, "Entity");
    if (!gScene) { lua_pushinteger(L, -1); return 1; }
    Entity e = gScene->CreateEntity(name);
    lua_pushinteger(L, (lua_Integer)(lua_Integer)e.GetID());
    return 1;
}

static int lua_destroyEntity(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx <= 0 || idx >= (int)gScene->Transforms.size())
        return 0; // ignore invalid / entity 0 (scene root)
    // Mark all components inactive so the entity is effectively removed at runtime
    gScene->Lights[idx].Active           = false;
    gScene->PlayerControllers[idx].Active = false;
    gScene->MouseLooks[idx].Active        = false;
    gScene->Triggers[idx].Active          = false;
    gScene->Animators[idx].Active         = false;
    gScene->LuaScripts[idx].Active        = false;
    gScene->MazeGenerators[idx].Active    = false;
    // Hide the entity name with a tombstone prefix so it won't be picked by findEntity
    gScene->EntityNames[idx] = "__destroyed__" + gScene->EntityNames[idx];
    return 0;
}

// ----------------------------------------------------------------
// Input queries accessible from Lua
// ----------------------------------------------------------------

static int lua_getMouseDelta(lua_State* L)
{
    MouseDelta d = InputManager::GetMouseDelta();
    lua_pushnumber(L, d.x);
    lua_pushnumber(L, d.y);
    return 2;
}

static int lua_isMouseDown(lua_State* L)
{
    int btn = (int)luaL_optinteger(L, 1, 0); // 0=Left 1=Right 2=Middle
    lua_pushboolean(L, InputManager::IsMouseDown(btn) ? 1 : 0);
    return 1;
}

static int lua_isKeyDown(lua_State* L)
{
    // Accepts either an integer GLFW key code or a string key name ("W", "Space", etc.)
    int code = -1;
    if (lua_type(L, 1) == LUA_TNUMBER)
        code = (int)lua_tointeger(L, 1);
    else if (lua_type(L, 1) == LUA_TSTRING)
        code = InputManager::KeyNameToCode(lua_tostring(L, 1));
    lua_pushboolean(L, (code >= 0 && InputManager::IsKeyDown(code)) ? 1 : 0);
    return 1;
}

// ----------------------------------------------------------------
// Register the `scene` table into the global state
// ----------------------------------------------------------------
static void RegisterSceneAPI()
{
    static const luaL_Reg kAPI[] = {
        { "getPos",         lua_getPos         },
        { "setPos",         lua_setPos         },
        { "getRot",         lua_getRot         },
        { "setRot",         lua_setRot         },
        { "getScale",       lua_getScale       },
        { "setScale",       lua_setScale       },
        { "getName",        lua_getName        },
        { "findEntity",     lua_findEntity     },
        { "getEntityCount", lua_getEntityCount },
        { "getChannel",     lua_getChannel     },
        { "setChannel",     lua_setChannel     },
        { "getPostEnabled", lua_getPostEnabled },
        { "setPostEnabled", lua_setPostEnabled },
        { "setExposure",    lua_setExposure    },
        { "setSaturation",  lua_setSaturation  },
        { "setContrast",    lua_setContrast    },
        { "setBrightness",  lua_setBrightness  },
        { "elapsed",        lua_elapsed        },
        { "spawnEntity",    lua_spawnEntity    },
        { "destroyEntity",  lua_destroyEntity  },
        { "getMouseDelta",  lua_getMouseDelta  },
        { "isMouseDown",    lua_isMouseDown    },
        { "isKeyDown",      lua_isKeyDown      },
        { nullptr, nullptr }
    };
    luaL_newlib(gL, kAPI);        // creates the table with all functions
    lua_setglobal(gL, "scene");   // scene.getPos(...) etc.
}

// ----------------------------------------------------------------
// Load a single script into a per-entity environment table.
// Returns true on success.
// ----------------------------------------------------------------
static bool LoadEntityScript(int entityIdx, const std::string& path)
{
    // --- Create environment table ---
    lua_newtable(gL);          // env table

    // Metatable: __index = _G  (so all globals are accessible)
    lua_newtable(gL);          // metatable
    lua_getglobal(gL, "_G");
    lua_setfield(gL, -2, "__index");
    lua_setmetatable(gL, -2);

    // Set entity_idx in env
    lua_pushinteger(gL, entityIdx);
    lua_setfield(gL, -2, "entity_idx");

    // Inject ExposedVars as Lua globals in the per-entity environment
    if (entityIdx < (int)gScene->LuaScripts.size()) {
        for (const auto& [k, v] : gScene->LuaScripts[entityIdx].ExposedVars) {
            bool isBool = (v == "true" || v == "false");
            char* end   = nullptr;
            float fv    = std::strtof(v.c_str(), &end);
            bool isNum  = !isBool && end && *end == '\0' && end != v.c_str();
            if (isBool)       lua_pushboolean(gL, v == "true" ? 1 : 0);
            else if (isNum)   lua_pushnumber (gL, (lua_Number)fv);
            else              lua_pushstring (gL, v.c_str());
            lua_setfield(gL, -2, k.c_str());
        }
    }

    // --- Load the script chunk ---
    if (luaL_loadfile(gL, path.c_str()) != LUA_OK)
    {
        const char* err = lua_tostring(gL, -1);
        fprintf(stderr, "[Lua] Load error entity %d (%s): %s\n", entityIdx, path.c_str(), err ? err : "?");
        lua_pop(gL, 2); // pop error + env
        return false;
    }

    // Set our env table as the chunk's _ENV upvalue (upvalue 1)
    lua_pushvalue(gL, -2); // copy env table on top
    if (lua_setupvalue(gL, -2, 1) == nullptr)
        lua_pop(gL, 1); // if no upvalue, pop the copy

    // --- Execute the chunk ---
    if (lua_pcall(gL, 0, 0, 0) != LUA_OK)
    {
        const char* err = lua_tostring(gL, -1);
        fprintf(stderr, "[Lua] Exec error entity %d (%s): %s\n", entityIdx, path.c_str(), err ? err : "?");
        lua_pop(gL, 2); // pop error + env
        return false;
    }

    // --- Store env ref and check which callbacks exist ---
    EntityScriptState st;
    lua_getfield(gL, -1, "OnStart");  st.hasStart  = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnUpdate"); st.hasUpdate = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnStop");   st.hasStop   = lua_isfunction(gL, -1); lua_pop(gL, 1);
    st.envRef = luaL_ref(gL, LUA_REGISTRYINDEX); // pops env table, stores in registry

    gStates[entityIdx] = st;
    return true;
}

// ----------------------------------------------------------------
// Helper: call a named zero-arg function in an entity env
// ----------------------------------------------------------------
static void CallEnvFunc0(const EntityScriptState& st, const char* fnName)
{
    lua_rawgeti(gL, LUA_REGISTRYINDEX, st.envRef); // push env table
    lua_getfield(gL, -1, fnName);                  // push function
    if (!lua_isfunction(gL, -1)) { lua_pop(gL, 2); return; }
    lua_remove(gL, -2);                            // remove env below function
    if (lua_pcall(gL, 0, 0, 0) != LUA_OK)
    {
        const char* err = lua_tostring(gL, -1);
        fprintf(stderr, "[Lua] %s error: %s\n", fnName, err ? err : "?");
        lua_pop(gL, 1);
    }
}

// ----------------------------------------------------------------
// LuaSystem public API
// ----------------------------------------------------------------

void LuaSystem::Init(Scene& scene)
{
    Shutdown(); // clean up any previous state

    gScene = &scene;
    gL     = luaL_newstate();
    luaL_openlibs(gL);
    RegisterSceneAPI();

    using namespace std::chrono;
    gStartTime = duration<double>(steady_clock::now().time_since_epoch()).count();

    // Load scripts for all active+enabled entities
    for (int i = 0; i < (int)scene.LuaScripts.size(); ++i)
    {
        const auto& ls = scene.LuaScripts[i];
        if (!ls.Active || !ls.Enabled || ls.ScriptPath.empty()) continue;
        LoadEntityScript(i, ls.ScriptPath);
    }
}

void LuaSystem::StartScripts()
{
    if (!gL) return;
    for (auto& [idx, st] : gStates)
        if (st.hasStart) CallEnvFunc0(st, "OnStart");
}

void LuaSystem::UpdateScripts(Scene& scene, float dt)
{
    if (!gL) return;
    gScene = &scene;
    for (auto& [idx, st] : gStates)
    {
        if (!st.hasUpdate) continue;
        // Check the entity is still active+enabled
        if (idx >= (int)scene.LuaScripts.size()) continue;
        if (!scene.LuaScripts[idx].Active || !scene.LuaScripts[idx].Enabled) continue;

        lua_rawgeti(gL, LUA_REGISTRYINDEX, st.envRef); // push env
        lua_getfield(gL, -1, "OnUpdate");               // push function
        if (!lua_isfunction(gL, -1)) { lua_pop(gL, 2); continue; }
        lua_remove(gL, -2);                             // remove env
        lua_pushnumber(gL, (lua_Number)dt);
        if (lua_pcall(gL, 1, 0, 0) != LUA_OK)
        {
            const char* err = lua_tostring(gL, -1);
            fprintf(stderr, "[Lua] OnUpdate error entity %d: %s\n", idx, err ? err : "?");
            lua_pop(gL, 1);
        }
    }
}

void LuaSystem::Shutdown()
{
    if (!gL) return;
    for (auto& [idx, st] : gStates)
        if (st.hasStop) CallEnvFunc0(st, "OnStop");
    for (auto& [idx, st] : gStates)
        if (st.envRef != LUA_NOREF) luaL_unref(gL, LUA_REGISTRYINDEX, st.envRef);
    gStates.clear();
    lua_close(gL);
    gL      = nullptr;
    gScene  = nullptr;
}

std::string LuaSystem::ExecString(const std::string& code)
{
    if (!gL) return "Lua not initialized";
    if (luaL_dostring(gL, code.c_str()) != LUA_OK)
    {
        const char* err = lua_tostring(gL, -1);
        std::string msg = err ? err : "unknown error";
        lua_pop(gL, 1);
        return msg;
    }
    return "";
}

} // namespace tsu
