#include "serialization/sceneSerializer.h"
#include "scene/scene.h"    // contains ColliderType, RigidBodyComponent, GameCameraComponent
#include "scene/entity.h"
#include "renderer/mesh.h"
#include "renderer/textureLoader.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

namespace tsu {

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static glm::vec3 parseVec3(const std::string& s)
{
    std::istringstream ss(s);
    glm::vec3 v{0,0,0};
    ss >> v.x >> v.y >> v.z;
    return v;
}

void SceneSerializer::Load(Scene& scene, const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "[SceneSerializer] Nao encontrado: " << filepath << "\n";
        return;
    }

    struct EntityData {
        std::string type     = "";
        std::string name     = "Entity";
        std::string color    = "#FFFFFF";
        std::string collider = "";
        glm::vec3   position = {0,0,0};
        glm::vec3   rotation = {0,0,0};
        glm::vec3   scale    = {1,1,1};
        bool        valid        = false;
        bool        hasRigidBody = false;
        bool        kinematic    = false;
        bool        useGravity   = true;
        float       mass         = 1.0f;
        bool        gravModule   = false;
        bool        colModule    = false;
        float       camFOV       = 75.0f;
        float       camYaw       = -90.0f;
        float       camPitch     = 0.0f;

        bool        hasPlayerController = false;
        std::string pcMode = "local";
        int         pcMovementMode = 2;   // 1=WorldCollision 2=LocalCollision 3=WorldNoclip 4=LocalNoclip
        float       pcWalkSpeed = 4.0f;
        float       pcRunMultiplier = 1.8f;
        float       pcCrouchMultiplier = 0.45f;
        int         pcKeyForward = 87;
        int         pcKeyBack    = 83;
        int         pcKeyLeft    = 65;
        int         pcKeyRight   = 68;
        int         pcKeyRun     = 340;
        int         pcKeyCrouch  = 341;
        int         pcChForward  = 0;
        int         pcChBack     = 1;
        int         pcChLeft     = 2;
        int         pcChRight    = 3;
        int         pcChRun      = 4;
        int         pcChCrouch   = 5;
        bool        pcAllowRun   = true;
        bool        pcAllowCrouch= true;

        // Mouse Look Component (standalone)
        bool  hasMouseLook     = false;
        int   mlYawTarget      = -1;
        int   mlPitchTarget    = -1;
        float mlSensX          = 0.15f;
        float mlSensY          = 0.15f;
        bool  mlInvertX        = false;
        bool  mlInvertY        = false;
        bool  mlClampPitch     = true;
        float mlPitchMin       = -89.0f;
        float mlPitchMax       =  89.0f;

        // Light
        bool        hasLight        = false;
        std::string lightType       = "directional"; // directional|point|spot|area
        glm::vec3   lightColor      = {1,1,1};
        float       lightTemp       = 6500.0f;
        float       lightIntensity  = 1.0f;
        float       lightRange      = 10.0f;
        float       lightInnerAngle = 30.0f;
        float       lightOuterAngle = 45.0f;
        float       lightWidth      = 1.0f;
        float       lightHeight     = 1.0f;
        int         materialIdx     = -1;   // index into scene.Materials

        // Maze Generator
        bool        hasMazeGen      = false;
        int         mgRoomSet       = -1;
        float       mgGenRadius     = 50.0f;
        float       mgDespawnRadius = 80.0f;
        float       mgBlockSize     = 2.0f;

        // Trigger Volume
        bool        hasTrigger      = false;
        glm::vec3   trigSize        = {1,2,1};
        glm::vec3   trigOffset      = {0,0,0};
        int         trigChannel     = -1;
        bool        trigInsideVal   = true;
        bool        trigOutsideVal  = false;
        bool        trigOneShot     = false;

        // Animator
        bool        hasAnimator     = false;
        int         animProp        = 0;   // AnimatorProperty
        int         animMode        = 0;   // AnimatorMode
        int         animEasing      = 0;   // AnimatorEasing
        glm::vec3   animFrom        = {0,0,0};
        glm::vec3   animTo          = {0,0,0};
        float       animDuration    = 1.0f;
        float       animDelay       = 0.0f;
        bool        animAutoPlay    = false;

        // Lua Script
        bool        hasLuaScript    = false;
        std::string luaScriptPath;
        std::map<std::string, std::string> luaExposedVars;
    };

    struct MaterialData {
        bool        valid      = false;
        int         index      = -1;   // 0-based index
        std::string name       = "Material";
        glm::vec3   color      = {1,1,1};
        std::string albedoPath;
        std::string normalPath;
        std::string aoPath;
        std::string roughPath;
        std::string metalPath;
        float roughness = 0.5f;
        float metallic  = 0.0f;
        float aoValue   = 1.0f;
        float tilingX   = 1.0f;
        float tilingY   = 1.0f;
        bool  worldSpaceUV = false;
    };

    auto flushMaterial = [&](MaterialData& d)
    {
        if (!d.valid || d.index < 0) return;
        // Grow Materials list to fit (scene may not have created them yet)
        while ((int)scene.Materials.size() <= d.index)
            scene.Materials.emplace_back();

        auto& mat       = scene.Materials[d.index];
        mat.Name        = d.name;
        mat.Color       = d.color;
        mat.AlbedoPath  = d.albedoPath;
        mat.NormalPath  = d.normalPath;
        mat.AOPath      = d.aoPath;
        mat.RoughnessPath = d.roughPath;
        mat.MetallicPath  = d.metalPath;
        mat.Roughness   = d.roughness;
        mat.Metallic    = d.metallic;
        mat.AOValue     = d.aoValue;
        mat.Tiling      = {d.tilingX, d.tilingY};
        mat.WorldSpaceUV = d.worldSpaceUV;
        mat.ORM_Dirty   = true;   // repack on first draw

        // Try to resolve texture IDs from already-loaded scene textures
        auto lookupOrLoad = [&](const std::string& p, bool linear) -> unsigned int {
            if (p.empty()) return 0;
            auto it = std::find(scene.Textures.begin(), scene.Textures.end(), p);
            if (it != scene.Textures.end()) {
                int idx = (int)(it - scene.Textures.begin());
                if (idx < (int)scene.TextureIDs.size()) return scene.TextureIDs[idx];
            }
            // Load directly
            unsigned int id = linear ? LoadTextureLinear(p) : LoadTexture(p);
            if (id) { scene.Textures.push_back(p); scene.TextureIDs.push_back(id); }
            return id;
        };
        mat.AlbedoID = lookupOrLoad(d.albedoPath, false);
        mat.NormalID = lookupOrLoad(d.normalPath, true);

        d = MaterialData{};
    };

    auto flushEntity = [&](EntityData& d)
    {
        if (!d.valid) return;

        Entity e    = scene.CreateEntity(d.name);
        uint32_t id = e.GetID();

        scene.Transforms[id].Position = d.position;
        scene.Transforms[id].Rotation = d.rotation;
        scene.Transforms[id].Scale    = d.scale;

        if (d.type == "game_camera")
        {
            GameCameraComponent& gc = scene.GameCameras[id];
            gc.Active = true;
            gc.FOV    = d.camFOV;
            gc.Yaw    = d.camYaw;
            gc.Pitch  = d.camPitch;
            gc.UpdateVectors();
        }
        else
        {
            Mesh* mesh = nullptr;
            if      (d.type == "cube")     mesh = new Mesh(Mesh::CreateCube(d.color));
            else if (d.type == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid(d.color));
            else if (d.type == "cylinder") mesh = new Mesh(Mesh::CreateCylinder(d.color));
            else if (d.type == "sphere")   mesh = new Mesh(Mesh::CreateSphere(d.color));
            else if (d.type == "capsule")  mesh = new Mesh(Mesh::CreateCapsule(d.color));
            else if (d.type == "plane")    mesh = new Mesh(Mesh::CreatePlane(d.color));
            else
                std::cerr << "[SceneSerializer] Tipo desconhecido: " << d.type << "\n";

            scene.MeshRenderers[id].MeshPtr  = mesh;
            scene.MeshRenderers[id].MeshType = d.type;

            if (d.hasRigidBody)
            {
                RigidBodyComponent& rb = scene.RigidBodies[id];
                rb.IsKinematic     = d.kinematic;
                rb.UseGravity      = d.useGravity;
                rb.Mass            = d.mass;
                // If explicit module flags were saved, use them; otherwise both on (legacy files)
                rb.HasGravityModule  = d.gravModule || (!d.gravModule && !d.colModule && d.hasRigidBody);
                rb.HasColliderModule = d.colModule  || (!d.gravModule && !d.colModule && d.hasRigidBody);

                if      (d.collider == "box")     rb.Collider = ColliderType::Box;
                else if (d.collider == "sphere")  rb.Collider = ColliderType::Sphere;
                else if (d.collider == "capsule") rb.Collider = ColliderType::Capsule;
                else if (d.collider == "pyramid") rb.Collider = ColliderType::Pyramid;
                else if (d.collider == "none")    rb.Collider = ColliderType::None;
                else
                {
                    // Auto-detecta pelo tipo de mesh
                    if      (d.type == "sphere")  rb.Collider = ColliderType::Sphere;
                    else if (d.type == "capsule") rb.Collider = ColliderType::Capsule;
                    else if (d.type == "pyramid") rb.Collider = ColliderType::Pyramid;
                    else                          rb.Collider = ColliderType::Box;
                }
            }
        }

        if (d.hasPlayerController)
        {
            auto& pc = scene.PlayerControllers[id];
            pc.Active = true;
            pc.InputMode = (d.pcMode == "channels") ? PlayerInputMode::Channels : PlayerInputMode::Local;
            pc.MovementMode = (PlayerMovementMode)d.pcMovementMode;
            pc.WalkSpeed = d.pcWalkSpeed;
            pc.RunMultiplier = d.pcRunMultiplier;
            pc.CrouchMultiplier = d.pcCrouchMultiplier;
            pc.KeyForward = d.pcKeyForward;
            pc.KeyBack    = d.pcKeyBack;
            pc.KeyLeft    = d.pcKeyLeft;
            pc.KeyRight   = d.pcKeyRight;
            pc.KeyRun     = d.pcKeyRun;
            pc.KeyCrouch  = d.pcKeyCrouch;
            pc.ChForward  = d.pcChForward;
            pc.ChBack     = d.pcChBack;
            pc.ChLeft     = d.pcChLeft;
            pc.ChRight    = d.pcChRight;
            pc.ChRun      = d.pcChRun;
            pc.ChCrouch   = d.pcChCrouch;
            pc.AllowRun   = d.pcAllowRun;
            pc.AllowCrouch= d.pcAllowCrouch;
        }

        if (d.hasMouseLook)
        {
            auto& ml = scene.MouseLooks[id];
            ml.Active          = true;
            ml.YawTargetEntity   = d.mlYawTarget;
            ml.PitchTargetEntity = d.mlPitchTarget;
            ml.SensitivityX    = d.mlSensX;
            ml.SensitivityY    = d.mlSensY;
            ml.InvertX         = d.mlInvertX;
            ml.InvertY         = d.mlInvertY;
            ml.ClampPitch      = d.mlClampPitch;
            ml.PitchMin        = d.mlPitchMin;
            ml.PitchMax        = d.mlPitchMax;
        }

        if (d.hasLight)
        {
            auto& lc = scene.Lights[id];
            lc.Active     = true;
            lc.Color      = d.lightColor;
            lc.Temperature= d.lightTemp;
            lc.Intensity  = d.lightIntensity;
            lc.Range      = d.lightRange;
            lc.InnerAngle = d.lightInnerAngle;
            lc.OuterAngle = d.lightOuterAngle;
            lc.Width      = d.lightWidth;
            lc.Height     = d.lightHeight;
            if      (d.lightType == "point")  lc.Type = LightType::Point;
            else if (d.lightType == "spot")   lc.Type = LightType::Spot;
            else if (d.lightType == "area")   lc.Type = LightType::Area;
            else                              lc.Type = LightType::Directional;
        }

        if (d.hasMazeGen)
        {
            auto& mg = scene.MazeGenerators[id];
            mg.Active         = true;
            mg.Enabled        = true;
            mg.RoomSetIndex   = d.mgRoomSet;
            mg.GenerateRadius = d.mgGenRadius;
            mg.DespawnRadius  = d.mgDespawnRadius;
            mg.BlockSize      = d.mgBlockSize;
        }

        if (d.hasTrigger)
        {
            auto& tr = scene.Triggers[id];
            tr.Active        = true;
            tr.Enabled       = true;
            tr.Size          = d.trigSize;
            tr.Offset        = d.trigOffset;
            tr.Channel       = d.trigChannel;
            tr.InsideValue   = d.trigInsideVal;
            tr.OutsideValue  = d.trigOutsideVal;
            tr.OneShot       = d.trigOneShot;
        }

        if (d.hasAnimator)
        {
            auto& an = scene.Animators[id];
            an.Active    = true;
            an.Enabled   = true;
            an.Property  = (AnimatorProperty)d.animProp;
            an.Mode      = (AnimatorMode)d.animMode;
            an.Easing    = (AnimatorEasing)d.animEasing;
            an.From      = d.animFrom;
            an.To        = d.animTo;
            an.Duration  = d.animDuration;
            an.Delay     = d.animDelay;
            an.AutoPlay  = d.animAutoPlay;
            an.Playing   = d.animAutoPlay;
        }

        if (d.hasLuaScript && id < (int)scene.LuaScripts.size())
        {
            auto& ls = scene.LuaScripts[id];
            ls.Active      = true;
            ls.Enabled     = true;
            ls.ScriptPath  = d.luaScriptPath;
            ls.ExposedVars = d.luaExposedVars;
        }

        d = EntityData{};
    };

    struct ChannelData {
        bool        valid  = false;
        int         index  = -1;
        std::string name   = "Channel";
        std::string type   = "bool";
        std::string value  = "false";
    };

    auto flushChannel = [&](ChannelData& c)
    {
        if (!c.valid) return;

        if (c.index < 0)
            c.index = (int)scene.Channels.size();
        scene.EnsureChannelCount(c.index + 1);

        auto& ch = scene.Channels[c.index];
        ch.Name = c.name;
        if (c.type == "float")
        {
            ch.Type = ChannelVariableType::Float;
            ch.FloatValue = std::stof(c.value);
        }
        else if (c.type == "string")
        {
            ch.Type = ChannelVariableType::String;
            ch.StringValue = c.value;
        }
        else
        {
            ch.Type = ChannelVariableType::Boolean;
            ch.BoolValue = (c.value == "true");
        }

        c = ChannelData{};
    };

    EntityData cur;
    ChannelData curCh;
    MaterialData curMat;

    // ---- Room set / room template data ----
    struct RoomSetData {
        bool valid = false;
        std::string name = "Room Set";
    };
    struct RoomData {
        bool valid = false;
        int setIdx = -1;   // which room set this room belongs to
        std::string name = "Room";
        float rarity = 1.0f;
        std::vector<RoomBlock> blocks;
        std::vector<DoorPlacement> doors;
        std::string lightmapPath;
        glm::vec4   lightmapST = {1,1,0,0};
    };
    RoomSetData curRs;
    RoomData    curRm;

    auto flushRoomSet = [&](RoomSetData& rs) {
        if (!rs.valid) return;
        RoomSet s;
        s.Name = rs.name;
        scene.RoomSets.push_back(s);
        rs = RoomSetData{};
    };

    auto flushRoom = [&](RoomData& rm) {
        if (!rm.valid) return;
        if (rm.setIdx >= 0 && rm.setIdx < (int)scene.RoomSets.size()) {
            RoomTemplate t;
            t.Name        = rm.name;
            t.Rarity      = rm.rarity;
            t.Blocks      = rm.blocks;
            t.Doors       = rm.doors;
            t.LightmapPath = rm.lightmapPath;
            t.LightmapST   = rm.lightmapST;
            scene.RoomSets[rm.setIdx].Rooms.push_back(t);
        }
        rm = RoomData{};
    };

    enum class Section { None, Entity, Channel, Material, RoomSet, Room, Fog, Sky, PostProcess, ScriptAssets };
    Section section = Section::None;
    std::string line;

    while (std::getline(file, line))
    {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[entity]")
        {
            flushChannel(curCh);
            flushEntity(cur);
            flushMaterial(curMat);
            flushRoom(curRm);
            flushRoomSet(curRs);
            cur.valid = true;
            section = Section::Entity;
            continue;
        }

        if (line == "[channel]")
        {
            flushEntity(cur);
            flushChannel(curCh);
            flushMaterial(curMat);
            flushRoom(curRm);
            flushRoomSet(curRs);
            curCh.valid = true;
            section = Section::Channel;
            continue;
        }

        if (line == "[material]")
        {
            flushEntity(cur);
            flushChannel(curCh);
            flushMaterial(curMat);
            flushRoom(curRm);
            flushRoomSet(curRs);
            curMat.valid = true;
            section = Section::Material;
            continue;
        }

        if (line == "[roomset]")
        {
            flushEntity(cur);
            flushChannel(curCh);
            flushMaterial(curMat);
            flushRoom(curRm);
            flushRoomSet(curRs);
            curRs.valid = true;
            section = Section::RoomSet;
            continue;
        }

        if (line == "[room]")
        {
            flushEntity(cur);
            flushChannel(curCh);
            flushMaterial(curMat);
            flushRoom(curRm);
            curRm.valid = true;
            curRm.setIdx = (int)scene.RoomSets.size() - 1; // belongs to last flushed set
            section = Section::Room;
            continue;
        }

        if (line == "[fog]")
        {
            flushEntity(cur); flushChannel(curCh); flushMaterial(curMat);
            flushRoom(curRm); flushRoomSet(curRs);
            section = Section::Fog;
            continue;
        }

        if (line == "[sky]")
        {
            flushEntity(cur); flushChannel(curCh); flushMaterial(curMat);
            flushRoom(curRm); flushRoomSet(curRs);
            section = Section::Sky;
            continue;
        }

        if (line == "[postprocess]")
        {
            flushEntity(cur); flushChannel(curCh); flushMaterial(curMat);
            flushRoom(curRm); flushRoomSet(curRs);
            section = Section::PostProcess;
            continue;
        }

        if (line == "[script_assets]")
        {
            flushEntity(cur); flushChannel(curCh); flushMaterial(curMat);
            flushRoom(curRm); flushRoomSet(curRs);
            section = Section::ScriptAssets;
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k   = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (section == Section::Entity)
        {
            if      (k == "type")      cur.type        = val;
            else if (k == "name")      cur.name        = val;
            else if (k == "color")     cur.color       = val;
            else if (k == "position")  cur.position    = parseVec3(val);
            else if (k == "rotation")  cur.rotation    = parseVec3(val);
            else if (k == "scale")     cur.scale       = parseVec3(val);
            else if (k == "rigidbody") cur.hasRigidBody= (val == "true");
            else if (k == "kinematic") cur.kinematic   = (val == "true");
            else if (k == "gravity")   cur.useGravity  = (val == "true");
            else if (k == "mass")      cur.mass        = std::stof(val);
            else if (k == "collider")  cur.collider    = val;
            else if (k == "fov")         cur.camFOV      = std::stof(val);
            else if (k == "yaw")         cur.camYaw      = std::stof(val);
            else if (k == "pitch")       cur.camPitch    = std::stof(val);
            else if (k == "grav_module") cur.gravModule  = (val == "true");
            else if (k == "col_module")  cur.colModule   = (val == "true");
            else if (k == "player_controller") cur.hasPlayerController = (val == "true");
            else if (k == "pc_move_mode")      cur.pcMovementMode = std::stoi(val);
            else if (k == "pc_mode")          cur.pcMode = val;
            else if (k == "pc_walk_speed")    cur.pcWalkSpeed = std::stof(val);
            else if (k == "pc_run_mult")      cur.pcRunMultiplier = std::stof(val);
            else if (k == "pc_crouch_mult")   cur.pcCrouchMultiplier = std::stof(val);
            else if (k == "pc_key_forward")   cur.pcKeyForward = std::stoi(val);
            else if (k == "pc_key_back")      cur.pcKeyBack = std::stoi(val);
            else if (k == "pc_key_left")      cur.pcKeyLeft = std::stoi(val);
            else if (k == "pc_key_right")     cur.pcKeyRight = std::stoi(val);
            else if (k == "pc_key_run")       cur.pcKeyRun = std::stoi(val);
            else if (k == "pc_key_crouch")    cur.pcKeyCrouch = std::stoi(val);
            else if (k == "pc_ch_forward")    cur.pcChForward = std::stoi(val);
            else if (k == "pc_ch_back")       cur.pcChBack = std::stoi(val);
            else if (k == "pc_ch_left")       cur.pcChLeft = std::stoi(val);
            else if (k == "pc_ch_right")      cur.pcChRight = std::stoi(val);
            else if (k == "pc_ch_run")        cur.pcChRun = std::stoi(val);
            else if (k == "pc_ch_crouch")     cur.pcChCrouch = std::stoi(val);
            else if (k == "pc_allow_run")     cur.pcAllowRun    = (val == "true");
            else if (k == "pc_allow_crouch")  cur.pcAllowCrouch = (val == "true");
            else if (k == "mouse_look")       cur.hasMouseLook  = (val == "true");
            else if (k == "ml_yaw_target")    cur.mlYawTarget   = std::stoi(val);
            else if (k == "ml_pitch_target")  cur.mlPitchTarget = std::stoi(val);
            else if (k == "ml_sens_x")        cur.mlSensX       = std::stof(val);
            else if (k == "ml_sens_y")        cur.mlSensY       = std::stof(val);
            else if (k == "ml_invert_x")      cur.mlInvertX     = (val == "true");
            else if (k == "ml_invert_y")      cur.mlInvertY     = (val == "true");
            else if (k == "ml_clamp_pitch")   cur.mlClampPitch  = (val == "true");
            else if (k == "ml_pitch_min")     cur.mlPitchMin    = std::stof(val);
            else if (k == "ml_pitch_max")     cur.mlPitchMax    = std::stof(val);
            else if (k == "light")            cur.hasLight           = (val == "true");
            else if (k == "light_type")       cur.lightType          = val;
            else if (k == "light_color")      cur.lightColor         = parseVec3(val);
            else if (k == "light_temp")       cur.lightTemp          = std::stof(val);
            else if (k == "light_intensity")  cur.lightIntensity     = std::stof(val);
            else if (k == "light_range")      cur.lightRange         = std::stof(val);
            else if (k == "light_inner")      cur.lightInnerAngle    = std::stof(val);
            else if (k == "light_outer")      cur.lightOuterAngle    = std::stof(val);
            else if (k == "light_width")      cur.lightWidth         = std::stof(val);
            else if (k == "light_height")     cur.lightHeight        = std::stof(val);
            else if (k == "entity_material")  cur.materialIdx        = std::stoi(val);
            else if (k == "maze_gen")         cur.hasMazeGen         = (val == "true");
            else if (k == "mg_room_set")      cur.mgRoomSet          = std::stoi(val);
            else if (k == "mg_gen_radius")    cur.mgGenRadius        = std::stof(val);
            else if (k == "mg_despawn_radius")cur.mgDespawnRadius    = std::stof(val);
            else if (k == "mg_block_size")    cur.mgBlockSize        = std::stof(val);
            // Trigger Volume
            else if (k == "trigger")          cur.hasTrigger         = (val == "true");
            else if (k == "trig_size")        cur.trigSize           = parseVec3(val);
            else if (k == "trig_offset")      cur.trigOffset         = parseVec3(val);
            else if (k == "trig_channel")     cur.trigChannel        = std::stoi(val);
            // legacy field fallback
            else if (k == "trig_enter_ch")    cur.trigChannel        = std::stoi(val);
            else if (k == "trig_inside_val" || k == "trig_enter_val")   cur.trigInsideVal   = (val == "true");
            else if (k == "trig_outside_val")  cur.trigOutsideVal  = (val == "false" ? false : (val == "true"));
            else if (k == "trig_one_shot")    cur.trigOneShot        = (val == "true");
            // Animator
            else if (k == "animator")         cur.hasAnimator        = (val == "true");
            else if (k == "anim_prop")        cur.animProp           = std::stoi(val);
            else if (k == "anim_mode")        cur.animMode           = std::stoi(val);
            else if (k == "anim_easing")      cur.animEasing         = std::stoi(val);
            else if (k == "anim_from")        cur.animFrom           = parseVec3(val);
            else if (k == "anim_to")          cur.animTo             = parseVec3(val);
            else if (k == "anim_duration")    cur.animDuration       = std::stof(val);
            else if (k == "anim_delay")       cur.animDelay          = std::stof(val);
            else if (k == "anim_auto_play")   cur.animAutoPlay       = (val == "true");
            // Lua Script
            else if (k == "lua_script")      cur.hasLuaScript       = (val == "true");
            else if (k == "lua_script_path") cur.luaScriptPath      = val;
            else if (k == "lua_var") {
                auto pipe = val.find('|');
                if (pipe != std::string::npos)
                    cur.luaExposedVars[val.substr(0, pipe)] = val.substr(pipe + 1);
            }
        }
        else if (section == Section::Fog)
        {
            if      (k == "type")    scene.Fog.Type    = (FogType)std::stoi(val);
            else if (k == "color")   scene.Fog.Color   = parseVec3(val);
            else if (k == "density") scene.Fog.Density = std::stof(val);
            else if (k == "start")   scene.Fog.Start   = std::stof(val);
            else if (k == "end")     scene.Fog.End     = std::stof(val);
        }
        else if (section == Section::Sky)
        {
            if      (k == "enabled") scene.Sky.Enabled         = (val == "true");
            else if (k == "zenith")  scene.Sky.ZenithColor     = parseVec3(val);
            else if (k == "horizon") scene.Sky.HorizonColor    = parseVec3(val);
            else if (k == "ground")  scene.Sky.GroundColor     = parseVec3(val);
            else if (k == "sun_dir") scene.Sky.SunDirection    = parseVec3(val);
            else if (k == "sun_col") scene.Sky.SunColor        = parseVec3(val);
            else if (k == "sun_size")  scene.Sky.SunSize       = std::stof(val);
            else if (k == "sun_bloom") scene.Sky.SunBloom      = std::stof(val);
        }
        else if (section == Section::PostProcess)
        {
            if      (k == "enabled")           scene.PostProcess.Enabled         = (val == "true");
            else if (k == "bloom")             scene.PostProcess.BloomEnabled    = (val == "true");
            else if (k == "bloom_threshold")   scene.PostProcess.BloomThreshold  = std::stof(val);
            else if (k == "bloom_intensity")   scene.PostProcess.BloomIntensity  = std::stof(val);
            else if (k == "bloom_radius")      scene.PostProcess.BloomRadius     = std::stof(val);
            else if (k == "vignette")          scene.PostProcess.VignetteEnabled = (val == "true");
            else if (k == "vig_radius")        scene.PostProcess.VignetteRadius  = std::stof(val);
            else if (k == "vig_strength")      scene.PostProcess.VignetteStrength= std::stof(val);
            else if (k == "grain")             scene.PostProcess.GrainEnabled    = (val == "true");
            else if (k == "grain_strength")    scene.PostProcess.GrainStrength   = std::stof(val);
            else if (k == "chroma")            scene.PostProcess.ChromaEnabled   = (val == "true");
            else if (k == "chroma_strength")   scene.PostProcess.ChromaStrength  = std::stof(val);
            else if (k == "color_grade")       scene.PostProcess.ColorGradeEnabled = (val == "true");
            else if (k == "exposure")          scene.PostProcess.Exposure        = std::stof(val);
            else if (k == "saturation")        scene.PostProcess.Saturation      = std::stof(val);
            else if (k == "contrast")          scene.PostProcess.Contrast        = std::stof(val);
            else if (k == "brightness")        scene.PostProcess.Brightness      = std::stof(val);
            else if (k == "sharpen")           scene.PostProcess.SharpenEnabled      = (val == "true");
            else if (k == "sharpen_strength")  scene.PostProcess.SharpenStrength     = std::stof(val);
            else if (k == "lens_distort")      scene.PostProcess.LensDistortEnabled  = (val == "true");
            else if (k == "lens_strength")     scene.PostProcess.LensDistortStrength = std::stof(val);
            else if (k == "sepia")             scene.PostProcess.SepiaEnabled        = (val == "true");
            else if (k == "sepia_strength")    scene.PostProcess.SepiaStrength       = std::stof(val);
            else if (k == "posterize")         scene.PostProcess.PosterizeEnabled    = (val == "true");
            else if (k == "posterize_levels")  scene.PostProcess.PosterizeLevels     = std::stof(val);
            else if (k == "scanlines")         scene.PostProcess.ScanlinesEnabled    = (val == "true");
            else if (k == "scanlines_strength")scene.PostProcess.ScanlinesStrength   = std::stof(val);
            else if (k == "scanlines_freq")    scene.PostProcess.ScanlinesFrequency  = std::stof(val);
            else if (k == "pixelate")          scene.PostProcess.PixelateEnabled     = (val == "true");
            else if (k == "pixelate_size")     scene.PostProcess.PixelateSize        = std::stof(val);
        }
        else if (section == Section::Material)
        {
            if      (k == "mat_index")     curMat.index     = std::stoi(val);
            else if (k == "mat_name")      curMat.name      = val;
            else if (k == "mat_color")     curMat.color     = parseVec3(val);
            else if (k == "mat_albedo")    curMat.albedoPath = val;
            else if (k == "mat_normal")    curMat.normalPath = val;
            else if (k == "mat_ao")        curMat.aoPath     = val;
            else if (k == "mat_roughness_tex") curMat.roughPath  = val;
            else if (k == "mat_metallic_tex")  curMat.metalPath  = val;
            else if (k == "mat_roughness") curMat.roughness  = std::stof(val);
            else if (k == "mat_metallic")  curMat.metallic   = std::stof(val);
            else if (k == "mat_ao_value")  curMat.aoValue    = std::stof(val);
            else if (k == "mat_tiling_x")  curMat.tilingX    = std::stof(val);
            else if (k == "mat_tiling_y")  curMat.tilingY    = std::stof(val);
            else if (k == "mat_world_uv")  curMat.worldSpaceUV = (val == "true");
        }
        else if (section == Section::Channel)
        {
            if      (k == "index") curCh.index = std::stoi(val) - 1;
            else if (k == "name")  curCh.name = val;
            else if (k == "type")  curCh.type = val;
            else if (k == "value") curCh.value = val;
        }
        else if (section == Section::RoomSet)
        {
            if (k == "name") curRs.name = val;
        }
        else if (section == Section::Room)
        {
            if      (k == "name")   curRm.name = val;
            else if (k == "rarity") curRm.rarity = std::stof(val);
            else if (k == "block")
            {
                // format: "x y z"
                std::istringstream bs(val);
                RoomBlock b;
                bs >> b.X >> b.Y >> b.Z;
                curRm.blocks.push_back(b);
            }
            else if (k == "door")
            {
                // format: "blockX blockZ blockY faceInt"
                std::istringstream ds(val);
                DoorPlacement d;
                int faceInt = 0;
                ds >> d.BlockX >> d.BlockZ >> d.BlockY >> faceInt;
                d.Face = (DoorFace)faceInt;
                curRm.doors.push_back(d);
            }
            else if (k == "lightmap_path") curRm.lightmapPath = val;
            else if (k == "lightmap_st")
            {
                std::istringstream ss(val);
                ss >> curRm.lightmapST.x >> curRm.lightmapST.y
                   >> curRm.lightmapST.z >> curRm.lightmapST.w;
            }
        }
        else if (section == Section::ScriptAssets)
        {
            if (k == "path")
            {
                // Add to ScriptAssets if not already present
                if (std::find(scene.ScriptAssets.begin(), scene.ScriptAssets.end(), val)
                    == scene.ScriptAssets.end())
                {
                    scene.ScriptAssets.push_back(val);
                    scene.ScriptAssetFolders.push_back(-1);
                }
            }
        }
    }

    flushEntity(cur);
    flushChannel(curCh);
    flushMaterial(curMat);
    flushRoom(curRm);
    flushRoomSet(curRs);
}

void SceneSerializer::Save(const Scene& scene, const std::string& filepath)
{
    std::ofstream file(filepath);
    if (!file.is_open())
    {
        std::cerr << "[SceneSerializer] Erro ao salvar: " << filepath << "\n";
        return;
    }

    file << "# tsuEngine Scene File\n\n";

    // ---- Materials ----
    for (size_t mi = 0; mi < scene.Materials.size(); ++mi)
    {
        const auto& mat = scene.Materials[mi];
        file << "[material]\n";
        file << "mat_index = "  << mi << "\n";
        file << "mat_name  = "  << mat.Name << "\n";
        file << "mat_color = "  << mat.Color.r << " " << mat.Color.g << " " << mat.Color.b << "\n";
        if (!mat.AlbedoPath.empty())    file << "mat_albedo       = " << mat.AlbedoPath    << "\n";
        if (!mat.NormalPath.empty())    file << "mat_normal       = " << mat.NormalPath    << "\n";
        if (!mat.AOPath.empty())        file << "mat_ao           = " << mat.AOPath         << "\n";
        if (!mat.RoughnessPath.empty()) file << "mat_roughness_tex = " << mat.RoughnessPath << "\n";
        if (!mat.MetallicPath.empty())  file << "mat_metallic_tex  = " << mat.MetallicPath  << "\n";
        file << "mat_roughness = " << mat.Roughness << "\n";
        file << "mat_metallic  = " << mat.Metallic  << "\n";
        file << "mat_ao_value  = " << mat.AOValue   << "\n";
        file << "mat_tiling_x  = " << mat.Tiling.x  << "\n";
        file << "mat_tiling_y  = " << mat.Tiling.y  << "\n";
        if (mat.WorldSpaceUV) file << "mat_world_uv  = true\n";
        file << "\n";
    }

    for (size_t i = 0; i < scene.Channels.size(); ++i)
    {
        const auto& ch = scene.Channels[i];
        file << "[channel]\n";
        file << "index = " << (i + 1) << "\n";
        file << "name  = " << ch.Name << "\n";
        if (ch.Type == ChannelVariableType::Float)
        {
            file << "type  = float\n";
            file << "value = " << ch.FloatValue << "\n\n";
        }
        else if (ch.Type == ChannelVariableType::String)
        {
            file << "type  = string\n";
            file << "value = " << ch.StringValue << "\n\n";
        }
        else
        {
            file << "type  = bool\n";
            file << "value = " << (ch.BoolValue ? "true" : "false") << "\n\n";
        }
    }

    // ---- Fog ----
    {
        const auto& f = scene.Fog;
        file << "[fog]\n";
        file << "type    = " << (int)f.Type << "\n";
        file << "color   = " << f.Color.r << " " << f.Color.g << " " << f.Color.b << "\n";
        file << "density = " << f.Density << "\n";
        file << "start   = " << f.Start   << "\n";
        file << "end     = " << f.End     << "\n\n";
    }

    // ---- Sky ----
    {
        const auto& s = scene.Sky;
        file << "[sky]\n";
        file << "enabled   = " << (s.Enabled ? "true" : "false") << "\n";
        file << "zenith    = " << s.ZenithColor.r  << " " << s.ZenithColor.g  << " " << s.ZenithColor.b  << "\n";
        file << "horizon   = " << s.HorizonColor.r << " " << s.HorizonColor.g << " " << s.HorizonColor.b << "\n";
        file << "ground    = " << s.GroundColor.r  << " " << s.GroundColor.g  << " " << s.GroundColor.b  << "\n";
        file << "sun_dir   = " << s.SunDirection.x << " " << s.SunDirection.y << " " << s.SunDirection.z << "\n";
        file << "sun_col   = " << s.SunColor.r << " " << s.SunColor.g << " " << s.SunColor.b << "\n";
        file << "sun_size  = " << s.SunSize  << "\n";
        file << "sun_bloom = " << s.SunBloom << "\n\n";
    }

    // ---- Post-Process ----
    {
        const auto& pp = scene.PostProcess;
        file << "[postprocess]\n";
        file << "enabled         = " << (pp.Enabled ? "true" : "false")         << "\n";
        file << "bloom           = " << (pp.BloomEnabled ? "true" : "false")    << "\n";
        file << "bloom_threshold = " << pp.BloomThreshold  << "\n";
        file << "bloom_intensity = " << pp.BloomIntensity  << "\n";
        file << "bloom_radius    = " << pp.BloomRadius     << "\n";
        file << "vignette        = " << (pp.VignetteEnabled ? "true" : "false") << "\n";
        file << "vig_radius      = " << pp.VignetteRadius  << "\n";
        file << "vig_strength    = " << pp.VignetteStrength<< "\n";
        file << "grain           = " << (pp.GrainEnabled ? "true" : "false")    << "\n";
        file << "grain_strength  = " << pp.GrainStrength   << "\n";
        file << "chroma          = " << (pp.ChromaEnabled ? "true" : "false")   << "\n";
        file << "chroma_strength = " << pp.ChromaStrength  << "\n";
        file << "color_grade     = " << (pp.ColorGradeEnabled ? "true" : "false") << "\n";
        file << "exposure        = " << pp.Exposure        << "\n";
        file << "saturation      = " << pp.Saturation      << "\n";
        file << "contrast        = " << pp.Contrast        << "\n";
        file << "brightness      = " << pp.Brightness      << "\n";
        file << "sharpen         = " << (pp.SharpenEnabled ? "true" : "false")      << "\n";
        file << "sharpen_strength= " << pp.SharpenStrength      << "\n";
        file << "lens_distort    = " << (pp.LensDistortEnabled ? "true" : "false")  << "\n";
        file << "lens_strength   = " << pp.LensDistortStrength   << "\n";
        file << "sepia           = " << (pp.SepiaEnabled ? "true" : "false")        << "\n";
        file << "sepia_strength  = " << pp.SepiaStrength          << "\n";
        file << "posterize       = " << (pp.PosterizeEnabled ? "true" : "false")    << "\n";
        file << "posterize_levels= " << pp.PosterizeLevels         << "\n";
        file << "scanlines       = " << (pp.ScanlinesEnabled ? "true" : "false")    << "\n";
        file << "scanlines_strength = " << pp.ScanlinesStrength    << "\n";
        file << "scanlines_freq  = " << pp.ScanlinesFrequency       << "\n";
        file << "pixelate        = " << (pp.PixelateEnabled ? "true" : "false")     << "\n";
        file << "pixelate_size   = " << pp.PixelateSize             << "\n\n";
    }

    for (size_t i = 0; i < scene.Transforms.size(); i++)
    {
        const TransformComponent&    t  = scene.Transforms[i];
        const MeshRendererComponent& mr = scene.MeshRenderers[i];
        const RigidBodyComponent&    rb = scene.RigidBodies[i];
        const GameCameraComponent&   gc = scene.GameCameras[i];

        file << "[entity]\n";

        // Name (always saved so it round-trips
        std::string entityName = (i < scene.EntityNames.size()) ? scene.EntityNames[i] : "Entity";
        file << "name  = " << entityName << "\n";

        if (i < scene.EntityMaterial.size() && scene.EntityMaterial[i] >= 0)
            file << "entity_material = " << scene.EntityMaterial[i] << "\n";

        if (gc.Active)
        {
            file << "type  = game_camera\n"
                 << "fov   = " << gc.FOV   << "\n"
                 << "yaw   = " << gc.Yaw   << "\n"
                 << "pitch = " << gc.Pitch << "\n";
        }
        else if (mr.MeshPtr)
        {
            // Write the actual mesh type stored in the component
            const std::string& mt = mr.MeshType;
            if      (mt == "sphere")   file << "type  = sphere\ncolor = #FFFFFF\n";
            else if (mt == "pyramid")  file << "type  = pyramid\ncolor = #FFFFFF\n";
            else if (mt == "cylinder") file << "type  = cylinder\ncolor = #FFFFFF\n";
            else if (mt == "capsule")  file << "type  = capsule\ncolor = #FFFFFF\n";
            else if (mt == "plane")    file << "type  = plane\ncolor = #FFFFFF\n";
            else                       file << "type  = cube\ncolor = #FFFFFF\n";
            if (rb.HasGravityModule || rb.HasColliderModule)
            {
                file << "rigidbody = true\n"
                     << "kinematic = " << (rb.IsKinematic ? "true" : "false") << "\n"
                     << "gravity   = " << (rb.UseGravity  ? "true" : "false") << "\n"
                     << "mass      = " << rb.Mass << "\n"
                     << "grav_module = " << (rb.HasGravityModule  ? "true" : "false") << "\n"
                     << "col_module  = " << (rb.HasColliderModule ? "true" : "false") << "\n";
            }
        }

        if (i < scene.PlayerControllers.size() && scene.PlayerControllers[i].Active)
        {
            const auto& pc = scene.PlayerControllers[i];
            file << "player_controller = true\n";
            file << "pc_move_mode = " << (int)pc.MovementMode << "\n";
            file << "pc_mode = "      << (pc.InputMode == PlayerInputMode::Channels ? "channels" : "local") << "\n";
            file << "pc_walk_speed = " << pc.WalkSpeed << "\n";
            file << "pc_run_mult = " << pc.RunMultiplier << "\n";
            file << "pc_crouch_mult = " << pc.CrouchMultiplier << "\n";
            file << "pc_key_forward = " << pc.KeyForward << "\n";
            file << "pc_key_back = " << pc.KeyBack << "\n";
            file << "pc_key_left = " << pc.KeyLeft << "\n";
            file << "pc_key_right = " << pc.KeyRight << "\n";
            file << "pc_key_run = " << pc.KeyRun << "\n";
            file << "pc_key_crouch = " << pc.KeyCrouch << "\n";
            file << "pc_ch_forward = " << pc.ChForward << "\n";
            file << "pc_ch_back = " << pc.ChBack << "\n";
            file << "pc_ch_left = " << pc.ChLeft << "\n";
            file << "pc_ch_right = " << pc.ChRight << "\n";
            file << "pc_ch_run = " << pc.ChRun << "\n";
            file << "pc_ch_crouch = " << pc.ChCrouch << "\n";
            file << "pc_allow_run = " << (pc.AllowRun ? "true" : "false") << "\n";
            file << "pc_allow_crouch = " << (pc.AllowCrouch ? "true" : "false") << "\n";
        }

        if (i < scene.MouseLooks.size() && scene.MouseLooks[i].Active)
        {
            const auto& ml = scene.MouseLooks[i];
            file << "mouse_look = true\n";
            file << "ml_yaw_target = "   << ml.YawTargetEntity   << "\n";
            file << "ml_pitch_target = " << ml.PitchTargetEntity << "\n";
            file << "ml_sens_x = "       << ml.SensitivityX      << "\n";
            file << "ml_sens_y = "       << ml.SensitivityY      << "\n";
            file << "ml_invert_x = "     << (ml.InvertX    ? "true" : "false") << "\n";
            file << "ml_invert_y = "     << (ml.InvertY    ? "true" : "false") << "\n";
            file << "ml_clamp_pitch = "  << (ml.ClampPitch ? "true" : "false") << "\n";
            file << "ml_pitch_min = "    << ml.PitchMin << "\n";
            file << "ml_pitch_max = "    << ml.PitchMax << "\n";
        }

        if (i < scene.Lights.size() && scene.Lights[i].Active)
        {
            const auto& lc = scene.Lights[i];
            static const char* ltNames[] = { "directional","point","spot","area" };
            file << "light           = true\n";
            file << "light_type      = " << ltNames[(int)lc.Type]   << "\n";
            file << "light_color     = " << lc.Color.r << " " << lc.Color.g << " " << lc.Color.b << "\n";
            file << "light_temp      = " << lc.Temperature          << "\n";
            file << "light_intensity = " << lc.Intensity            << "\n";
            file << "light_range     = " << lc.Range                << "\n";
            file << "light_inner     = " << lc.InnerAngle           << "\n";
            file << "light_outer     = " << lc.OuterAngle           << "\n";
            file << "light_width     = " << lc.Width                << "\n";
            file << "light_height    = " << lc.Height               << "\n";
        }

        if (i < scene.MazeGenerators.size() && scene.MazeGenerators[i].Active)
        {
            const auto& mg = scene.MazeGenerators[i];
            file << "maze_gen          = true\n";
            file << "mg_room_set       = " << mg.RoomSetIndex   << "\n";
            file << "mg_gen_radius     = " << mg.GenerateRadius << "\n";
            file << "mg_despawn_radius = " << mg.DespawnRadius  << "\n";
            file << "mg_block_size     = " << mg.BlockSize      << "\n";
        }

        if (i < scene.Triggers.size() && scene.Triggers[i].Active)
        {
            const auto& tr = scene.Triggers[i];
            file << "trigger       = true\n";
            file << "trig_size     = " << tr.Size.x << " " << tr.Size.y << " " << tr.Size.z << "\n";
            file << "trig_offset   = " << tr.Offset.x << " " << tr.Offset.y << " " << tr.Offset.z << "\n";
            file << "trig_channel   = " << tr.Channel << "\n";
            file << "trig_inside_val = " << (tr.InsideValue  ? "true" : "false") << "\n";
            file << "trig_outside_val = " << (tr.OutsideValue ? "true" : "false") << "\n";
            file << "trig_one_shot  = " << (tr.OneShot ? "true" : "false") << "\n";
        }

        if (i < scene.Animators.size() && scene.Animators[i].Active)
        {
            const auto& an = scene.Animators[i];
            file << "animator      = true\n";
            file << "anim_prop     = " << (int)an.Property << "\n";
            file << "anim_mode     = " << (int)an.Mode     << "\n";
            file << "anim_easing   = " << (int)an.Easing   << "\n";
            file << "anim_from     = " << an.From.x << " " << an.From.y << " " << an.From.z << "\n";
            file << "anim_to       = " << an.To.x   << " " << an.To.y   << " " << an.To.z   << "\n";
            file << "anim_duration = " << an.Duration << "\n";
            file << "anim_delay    = " << an.Delay    << "\n";
            file << "anim_auto_play = " << (an.AutoPlay ? "true" : "false") << "\n";
        }

        if (i < scene.LuaScripts.size() && scene.LuaScripts[i].Active)
        {
            const auto& ls = scene.LuaScripts[i];
            file << "lua_script      = true\n";
            file << "lua_script_path = " << ls.ScriptPath << "\n";
            for (const auto& [vk, vv] : ls.ExposedVars)
                file << "lua_var = " << vk << "|" << vv << "\n";
        }

        file << "position = " << t.Position.x << " " << t.Position.y << " " << t.Position.z << "\n"
             << "rotation = " << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << "\n"
             << "scale    = " << t.Scale.x    << " " << t.Scale.y    << " " << t.Scale.z    << "\n\n";
    }

    // ---- Room Sets ----
    for (size_t si = 0; si < scene.RoomSets.size(); ++si)
    {
        const auto& rs = scene.RoomSets[si];
        file << "[roomset]\n";
        file << "name = " << rs.Name << "\n\n";

        for (const auto& room : rs.Rooms)
        {
            file << "[room]\n";
            file << "name   = " << room.Name << "\n";
            file << "rarity = " << room.Rarity << "\n";
            for (const auto& b : room.Blocks)
                file << "block = " << b.X << " " << b.Y << " " << b.Z << "\n";
            for (const auto& d : room.Doors)
                file << "door = " << d.BlockX << " " << d.BlockZ << " " << d.BlockY << " " << (int)d.Face << "\n";
            if (!room.LightmapPath.empty())
            {
                file << "lightmap_path = " << room.LightmapPath << "\n";
                const auto& st = room.LightmapST;
                file << "lightmap_st = " << st.x << " " << st.y << " " << st.z << " " << st.w << "\n";
            }
            file << "\n";
        }
    }

    // ---- Script Assets ----
    if (!scene.ScriptAssets.empty())
    {
        file << "[script_assets]\n";
        for (const auto& p : scene.ScriptAssets)
            file << "path = " << p << "\n";
        file << "\n";
    }
}

} // namespace tsu