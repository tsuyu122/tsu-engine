#include "lua/luaSystem.h"
#include "scene/scene.h"
#include "scene/entity.h"
#include "input/inputmanager.h"
#include "network/multiplayerSystem.h"
#include <GLFW/glfw3.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

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

static lua_State*  gL         = nullptr;
static Scene*       gScene          = nullptr;
static double       gStartTime      = 0.0;
static GLFWwindow*  gWindow         = nullptr;
static std::string  gPendingLoadScene;  // set by lua_loadScene, consumed by Application

// Per-entity Lua environment data
struct EntityScriptState
{
    int  envRef          = LUA_NOREF; // registry ref to the per-entity env table
    bool hasStart        = false;
    bool hasUpdate       = false;
    bool hasStop         = false;
    bool hasTriggerEnter = false;
    bool hasTriggerExit  = false;
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
    if (!gScene || idx < 0)
    { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, gScene->GetChannelBool(idx, false) ? 1 : 0);
    return 1;
}

static int lua_setChannel(lua_State* L)
{
    int  idx = (int)luaL_checkinteger(L, 1);
    bool val = lua_toboolean(L, 2) != 0;
    if (gScene && idx >= 0)
        gScene->SetChannelBool(idx, val);
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

static int lua_isConnected(lua_State* L)
{
    lua_pushboolean(L, (gScene && MultiplayerSystem::IsConnected(*gScene)) ? 1 : 0);
    return 1;
}

static int lua_isHost(lua_State* L)
{
    lua_pushboolean(L, (gScene && MultiplayerSystem::IsHost(*gScene)) ? 1 : 0);
    return 1;
}

static int lua_playerCount(lua_State* L)
{
    lua_pushinteger(L, gScene ? MultiplayerSystem::GetPlayerCount(*gScene) : 0);
    return 1;
}

static int lua_getDistance(lua_State* L)
{
    int a = (int)luaL_checkinteger(L, 1);
    int b = (int)luaL_checkinteger(L, 2);
    if (!gScene || a < 0 || b < 0 || a >= (int)gScene->Transforms.size() || b >= (int)gScene->Transforms.size())
    {
        lua_pushnumber(L, 0.0);
        return 1;
    }
    glm::vec3 pa = gScene->GetEntityWorldPos(a);
    glm::vec3 pb = gScene->GetEntityWorldPos(b);
    lua_pushnumber(L, glm::length(pa - pb));
    return 1;
}

static int lua_getTag(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->EntityTags.size())
    {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushstring(L, gScene->EntityTags[idx].c_str());
    return 1;
}

static int lua_setTag(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    const char* tag = luaL_checkstring(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->Transforms.size()) return 0;
    while (idx >= (int)gScene->EntityTags.size()) gScene->EntityTags.push_back("");
    gScene->EntityTags[idx] = tag ? tag : "";
    return 0;
}

static int lua_findEntityByTag(lua_State* L)
{
    const char* tag = luaL_checkstring(L, 1);
    if (!gScene || !tag) { lua_pushinteger(L, -1); return 1; }
    for (int i = 0; i < (int)gScene->EntityTags.size(); ++i)
        if (gScene->EntityTags[i] == tag) { lua_pushinteger(L, i); return 1; }
    lua_pushinteger(L, -1);
    return 1;
}

static int lua_getNetworkId(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->MultiplayerControllers.size())
    {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, (lua_Integer)gScene->MultiplayerControllers[idx].NetworkId);
    return 1;
}

static int lua_setNickname(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    const char* nick = luaL_checkstring(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->MultiplayerControllers.size() || !nick) return 0;
    auto& mc = gScene->MultiplayerControllers[idx];
    mc.Active = true;
    mc.Nickname = nick;
    if (mc.NetworkId == 0) mc.NetworkId = MultiplayerSystem::MakeNetworkId(mc.Nickname);
    return 0;
}

static int lua_setAnimatorPlaying(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    int play = (int)luaL_checkinteger(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->Animators.size()) return 0;
    gScene->Animators[idx].Playing = (play != 0);
    return 0;
}

static int lua_setAnimatorSpeed(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    float sp = (float)luaL_checknumber(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->Animators.size()) return 0;
    gScene->Animators[idx].PlaybackSpeed = sp;
    return 0;
}

static int lua_setAnimatorBlend(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    float bw = (float)luaL_checknumber(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->Animators.size()) return 0;
    gScene->Animators[idx].BlendWeight = bw;
    return 0;
}

static int lua_setAnimControllerState(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    int st  = (int)luaL_checkinteger(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->AnimationControllers.size()) return 0;
    auto& ac = gScene->AnimationControllers[idx];
    if (st >= 0 && st < (int)ac.States.size()) { ac._CurrentState = st; ac._StateTime = 0.0f; }
    return 0;
}

static int lua_getAnimControllerState(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->AnimationControllers.size()) { lua_pushinteger(L, -1); return 1; }
    lua_pushinteger(L, gScene->AnimationControllers[idx]._CurrentState);
    return 1;
}

static int lua_setAnimControllerEnabled(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    int en  = (int)luaL_checkinteger(L, 2);
    if (!gScene || idx < 0 || idx >= (int)gScene->AnimationControllers.size()) return 0;
    gScene->AnimationControllers[idx].Enabled = (en != 0);
    return 0;
}

static int lua_setSSAO(lua_State* L)
{
    int en = (int)luaL_checkinteger(L, 1);
    float intensity = (float)luaL_optnumber(L, 2, 0.35);
    float radius    = (float)luaL_optnumber(L, 3, 2.0);
    if (!gScene) return 0;
    gScene->PostProcess.SSAOEnabled   = (en != 0);
    gScene->PostProcess.SSAOIntensity = intensity;
    gScene->PostProcess.SSAORadius    = radius;
    return 0;
}

static int lua_setDistanceCulling(lua_State* L)
{
    int en = (int)luaL_checkinteger(L, 1);
    float dist = (float)luaL_optnumber(L, 2, 150.0);
    if (!gScene) return 0;
    gScene->Realtime.DistanceCulling = (en != 0);
    gScene->Realtime.MaxDrawDistance = dist;
    return 0;
}

static int lua_getLightmapIntensity(lua_State* L)
{
    lua_pushnumber(L, gScene ? gScene->LightmapIntensity : 0.0);
    return 1;
}

static int lua_setLightmapIntensity(lua_State* L)
{
    float v = (float)luaL_checknumber(L, 1);
    if (!gScene) return 0;
    gScene->LightmapIntensity = v;
    return 0;
}

static int lua_playAudio(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->AudioSources.size()) return 0;
    gScene->AudioSources[idx]._Playing = true;
    return 0;
}

static int lua_getAudioGain(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    float gain = 0.0f;
    if (gScene && idx >= 0 && idx < (int)gScene->AudioSources.size())
        gain = gScene->AudioSources[idx]._CurrentGain;
    lua_pushnumber(L, gain);
    return 1;
}

static int lua_loadSkeleton(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    const char* path = luaL_checkstring(L, 2);
    if (!gScene || !path) { lua_pushboolean(L, 0); return 1; }
    if (idx < 0 || idx >= (int)gScene->SkinnedMeshes.size()) { lua_pushboolean(L, 0); return 1; }
    // Application-level helper not visible here; do minimal parse via Scene
    // Mark component active and store path; Application will load on next frame
    auto& sk = gScene->SkinnedMeshes[idx];
    sk.Active = true;
    sk.Enabled = true;
    sk.SkeletonPath = path;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_setBonePose(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    int bone = (int)luaL_checkinteger(L, 2);
    float tx = (float)luaL_checknumber(L, 3);
    float ty = (float)luaL_checknumber(L, 4);
    float tz = (float)luaL_checknumber(L, 5);
    float rx = (float)luaL_checknumber(L, 6);
    float ry = (float)luaL_checknumber(L, 7);
    float rz = (float)luaL_checknumber(L, 8);
    if (!gScene || idx < 0 || idx >= (int)gScene->SkinnedMeshes.size()) return 0;
    auto& sk = gScene->SkinnedMeshes[idx];
    if (bone < 0 || bone >= sk.BoneCount) return 0;
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(tx,ty,tz));
    glm::mat4 R = glm::yawPitchRoll(glm::radians(ry), glm::radians(rx), glm::radians(rz));
    sk.Pose[bone] = T * R;
    return 0;
}
// ----------------------------------------------------------------
// Entity spawn / destroy
// ----------------------------------------------------------------

static int lua_spawnEntity(lua_State* L)
{
    const char* name = luaL_optstring(L, 1, "Entity");
    if (!gScene) { lua_pushinteger(L, -1); return 1; }
    Entity e = gScene->CreateEntity(name);
    lua_pushinteger(L, (lua_Integer)e.GetID());
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
    if (idx < (int)gScene->MeshRenderers.size())
    {
        auto& mr = gScene->MeshRenderers[idx];
        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
        mr.MeshType.clear();
    }
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
// Channel float / string access
// ----------------------------------------------------------------

static int lua_getChannelFloat(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    lua_pushnumber(L, gScene ? gScene->GetChannelFloat(idx, 0.0f) : 0.0f);
    return 1;
}

static int lua_setChannelFloat(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float val = (float)luaL_checknumber(L, 2);
    if (gScene) gScene->SetChannelFloat(idx, val);
    return 0;
}

static int lua_getChannelString(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    std::string s = gScene ? gScene->GetChannelString(idx, "") : "";
    lua_pushstring(L, s.c_str());
    return 1;
}

static int lua_setChannelString(lua_State* L)
{
    int         idx = (int)luaL_checkinteger(L, 1);
    const char* val = luaL_checkstring(L, 2);
    if (gScene) gScene->SetChannelString(idx, val);
    return 0;
}

// ----------------------------------------------------------------
// Physics / RigidBody access
// ----------------------------------------------------------------

static int lua_getVelocity(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    if (!gScene || idx < 0 || idx >= (int)gScene->RigidBodies.size())
    { lua_pushnumber(L,0); lua_pushnumber(L,0); lua_pushnumber(L,0); return 3; }
    auto& v = gScene->RigidBodies[idx].Velocity;
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    lua_pushnumber(L, v.z);
    return 3;
}

static int lua_setVelocity(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float x   = (float)luaL_checknumber(L, 2);
    float y   = (float)luaL_checknumber(L, 3);
    float z   = (float)luaL_checknumber(L, 4);
    if (gScene && idx >= 0 && idx < (int)gScene->RigidBodies.size())
        gScene->RigidBodies[idx].Velocity = { x, y, z };
    return 0;
}

static int lua_isGrounded(lua_State* L)
{
    int idx = (int)luaL_checkinteger(L, 1);
    bool grounded = false;
    if (gScene && idx >= 0 && idx < (int)gScene->RigidBodies.size())
        grounded = gScene->RigidBodies[idx].IsGrounded;
    lua_pushboolean(L, grounded ? 1 : 0);
    return 1;
}

static int lua_applyImpulse(lua_State* L)
{
    int   idx = (int)luaL_checkinteger(L, 1);
    float x   = (float)luaL_checknumber(L, 2);
    float y   = (float)luaL_checknumber(L, 3);
    float z   = (float)luaL_checknumber(L, 4);
    if (gScene && idx >= 0 && idx < (int)gScene->RigidBodies.size())
    {
        auto& rb = gScene->RigidBodies[idx];
        if (rb.HasGravityModule && rb.Mass > 0.0f)
            rb.Velocity += glm::vec3(x, y, z) / rb.Mass;
        else
            rb.Velocity += glm::vec3(x, y, z);
    }
    return 0;
}

// ----------------------------------------------------------------
// Cursor control
// ----------------------------------------------------------------

static int lua_setCursorLocked(lua_State* L)
{
    bool locked = lua_toboolean(L, 1) != 0;
    if (gWindow)
        glfwSetInputMode(gWindow, GLFW_CURSOR,
                         locked ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    return 0;
}

static int lua_isCursorLocked(lua_State* L)
{
    bool locked = false;
    if (gWindow)
        locked = (glfwGetInputMode(gWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED);
    lua_pushboolean(L, locked ? 1 : 0);
    return 1;
}

// scene.loadScene(path) — request the Application to load a new scene next frame
static int lua_loadScene(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    LuaSystem::RequestLoadScene(path);
    return 0;
}

// ----------------------------------------------------------------
// Register the `scene` table into the global state
// ----------------------------------------------------------------
static void RegisterSceneAPI()
{
    static const luaL_Reg kAPI[] = {
        { "getPos",            lua_getPos            },
        { "setPos",            lua_setPos            },
        { "getRot",            lua_getRot            },
        { "setRot",            lua_setRot            },
        { "getScale",          lua_getScale          },
        { "setScale",          lua_setScale          },
        { "getName",           lua_getName           },
        { "findEntity",        lua_findEntity        },
        { "getEntityCount",    lua_getEntityCount    },
        { "getChannel",        lua_getChannel        },
        { "setChannel",        lua_setChannel        },
        { "getChannelFloat",   lua_getChannelFloat   },
        { "setChannelFloat",   lua_setChannelFloat   },
        { "getChannelString",  lua_getChannelString  },
        { "setChannelString",  lua_setChannelString  },
        { "getVelocity",       lua_getVelocity       },
        { "setVelocity",       lua_setVelocity       },
        { "isGrounded",        lua_isGrounded        },
        { "applyImpulse",      lua_applyImpulse      },
        { "setCursorLocked",   lua_setCursorLocked   },
        { "isCursorLocked",    lua_isCursorLocked    },
        { "loadScene",         lua_loadScene         },
        { "getPostEnabled",    lua_getPostEnabled    },
        { "setPostEnabled",    lua_setPostEnabled    },
        { "setExposure",       lua_setExposure       },
        { "setSaturation",     lua_setSaturation     },
        { "setContrast",       lua_setContrast       },
        { "setBrightness",     lua_setBrightness     },
        { "elapsed",           lua_elapsed           },
        { "isConnected",       lua_isConnected       },
        { "isHost",            lua_isHost            },
        { "playerCount",       lua_playerCount       },
        { "getDistance",       lua_getDistance       },
        { "getTag",            lua_getTag            },
        { "setTag",            lua_setTag            },
        { "findEntityByTag",   lua_findEntityByTag   },
        { "getNetworkId",      lua_getNetworkId      },
        { "setNickname",       lua_setNickname       },
        { "setAnimatorPlaying",lua_setAnimatorPlaying},
        { "setAnimatorSpeed",  lua_setAnimatorSpeed  },
        { "setAnimatorBlend",  lua_setAnimatorBlend  },
        { "setAnimControllerState", lua_setAnimControllerState },
        { "getAnimControllerState", lua_getAnimControllerState },
        { "setAnimControllerEnabled", lua_setAnimControllerEnabled },
        { "setSSAO",            lua_setSSAO            },
        { "setDistanceCulling", lua_setDistanceCulling },
        { "getLightmapIntensity", lua_getLightmapIntensity },
        { "setLightmapIntensity", lua_setLightmapIntensity },
        { "playAudio",          lua_playAudio          },
        { "getAudioGain",       lua_getAudioGain       },
        { "loadSkeleton",       lua_loadSkeleton       },
        { "setBonePose",        lua_setBonePose        },
        { "spawnEntity",       lua_spawnEntity       },
        { "destroyEntity",     lua_destroyEntity     },
        { "getMouseDelta",     lua_getMouseDelta     },
        { "isMouseDown",       lua_isMouseDown       },
        { "isKeyDown",         lua_isKeyDown         },
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
    lua_getfield(gL, -1, "OnStart");        st.hasStart        = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnUpdate");       st.hasUpdate       = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnStop");         st.hasStop         = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnTriggerEnter"); st.hasTriggerEnter = lua_isfunction(gL, -1); lua_pop(gL, 1);
    lua_getfield(gL, -1, "OnTriggerExit");  st.hasTriggerExit  = lua_isfunction(gL, -1); lua_pop(gL, 1);
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

void LuaSystem::SetWindow(GLFWwindow* window)
{
    gWindow = window;
}

void LuaSystem::RequestLoadScene(const std::string& path)
{
    gPendingLoadScene = path;
}

std::string LuaSystem::GetPendingLoadScene()
{
    std::string result = gPendingLoadScene;
    gPendingLoadScene.clear();
    return result;
}

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

static void CallEnvFunc1i(const EntityScriptState& st, const char* fnName, int arg)
{
    lua_rawgeti(gL, LUA_REGISTRYINDEX, st.envRef);
    lua_getfield(gL, -1, fnName);
    if (!lua_isfunction(gL, -1)) { lua_pop(gL, 2); return; }
    lua_remove(gL, -2);
    lua_pushinteger(gL, arg);
    if (lua_pcall(gL, 1, 0, 0) != LUA_OK)
    {
        const char* err = lua_tostring(gL, -1);
        fprintf(stderr, "[Lua] %s error: %s\n", fnName, err ? err : "?");
        lua_pop(gL, 1);
    }
}

void LuaSystem::FireTriggerEnter(int triggerEntityIdx)
{
    if (!gL) return;
    for (auto& [idx, st] : gStates)
        if (st.hasTriggerEnter) CallEnvFunc1i(st, "OnTriggerEnter", triggerEntityIdx);
}

void LuaSystem::FireTriggerExit(int triggerEntityIdx)
{
    if (!gL) return;
    for (auto& [idx, st] : gStates)
        if (st.hasTriggerExit) CallEnvFunc1i(st, "OnTriggerExit", triggerEntityIdx);
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
