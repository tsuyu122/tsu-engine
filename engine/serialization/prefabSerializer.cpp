#include "serialization/prefabSerializer.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace tsu {

static std::string ptrim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static glm::vec3 pParseVec3(const std::string& s)
{
    std::istringstream ss(s);
    glm::vec3 v{0,0,0};
    ss >> v.x >> v.y >> v.z;
    return v;
}

// ----------------------------------------------------------------
// Save  – writes one [node] section per entry in p.Nodes
// ----------------------------------------------------------------
void PrefabSerializer::Save(const PrefabAsset& p, const std::string& filepath)
{
    std::ofstream f(filepath);
    if (!f.is_open())
    {
        std::cerr << "[PrefabSerializer] Cannot open for write: " << filepath << "\n";
        return;
    }

    f << "# tsuEngine Prefab File\n";
    f << "name = " << p.Name << "\n\n";

    for (int ni = 0; ni < (int)p.Nodes.size(); ++ni)
    {
        const PrefabEntityData& n = p.Nodes[ni];
        f << "[node]\n";
        f << "node_idx    = " << ni           << "\n";
        f << "node_name   = " << n.Name       << "\n";
        f << "node_parent = " << n.ParentIdx  << "\n";
        f << "node_guid   = " << n.NodeGuid   << "\n";

        if (!n.MeshType.empty())     f << "node_mesh     = " << n.MeshType     << "\n";
        if (!n.MaterialName.empty()) f << "node_material = " << n.MaterialName << "\n";

        const auto& t = n.Transform;
        f << "position = " << t.Position.x << " " << t.Position.y << " " << t.Position.z << "\n";
        f << "rotation = " << t.Rotation.x << " " << t.Rotation.y << " " << t.Rotation.z << "\n";
        f << "scale    = " << t.Scale.x    << " " << t.Scale.y    << " " << t.Scale.z    << "\n";

        if (n.HasLight)
        {
            static const char* ltNames[] = {"directional","point","spot","area"};
            const LightComponent& lc = n.Light;
            f << "has_light       = true\n";
            f << "light_type      = " << ltNames[(int)lc.Type] << "\n";
            f << "light_color     = " << lc.Color.r << " " << lc.Color.g << " " << lc.Color.b << "\n";
            f << "light_temp      = " << lc.Temperature  << "\n";
            f << "light_intensity = " << lc.Intensity    << "\n";
            f << "light_range     = " << lc.Range        << "\n";
            f << "light_inner     = " << lc.InnerAngle   << "\n";
            f << "light_outer     = " << lc.OuterAngle   << "\n";
            f << "light_width     = " << lc.Width        << "\n";
            f << "light_height    = " << lc.Height       << "\n";
        }

        if (n.HasCamera)
        {
            const GameCameraComponent& gc = n.Camera;
            f << "has_camera = true\n";
            f << "cam_fov    = " << gc.FOV   << "\n";
            f << "cam_near   = " << gc.Near  << "\n";
            f << "cam_far    = " << gc.Far   << "\n";
            f << "cam_yaw    = " << gc.Yaw   << "\n";
            f << "cam_pitch  = " << gc.Pitch << "\n";
        }

        if (n.HasTrigger)
        {
            f << "has_trigger = true\n";
            f << "trig_size = " << n.TriggerSize.x << " " << n.TriggerSize.y << " " << n.TriggerSize.z << "\n";
            f << "trig_offset = " << n.TriggerOffset.x << " " << n.TriggerOffset.y << " " << n.TriggerOffset.z << "\n";
            f << "trig_channel = " << n.TriggerChannel << "\n";
            f << "trig_inside = " << (n.TriggerInsideValue ? "true" : "false") << "\n";
            f << "trig_outside = " << (n.TriggerOutsideValue ? "true" : "false") << "\n";
            f << "trig_oneshot = " << (n.TriggerOneShot ? "true" : "false") << "\n";
        }
        // Entity flags and tag
        f << "entity_static = " << (n.EntityStatic ? "true" : "false") << "\n";
        f << "receive_lightmap = " << (n.ReceiveLightmap ? "true" : "false") << "\n";
        if (!n.Tag.empty()) f << "entity_tag = " << n.Tag << "\n";
        if (n.HasAnimator)
        {
            f << "has_animator = true\n";
            f << "anim_prop = " << n.AnimProp << "\n";
            f << "anim_mode = " << n.AnimMode << "\n";
            f << "anim_ease = " << n.AnimEasing << "\n";
            f << "anim_from = " << n.AnimFrom.x << " " << n.AnimFrom.y << " " << n.AnimFrom.z << "\n";
            f << "anim_to = " << n.AnimTo.x << " " << n.AnimTo.y << " " << n.AnimTo.z << "\n";
            f << "anim_duration = " << n.AnimDuration << "\n";
            f << "anim_delay = " << n.AnimDelay << "\n";
            f << "anim_speed = " << n.AnimSpeed << "\n";
            f << "anim_blend = " << n.AnimBlend << "\n";
            f << "anim_auto = " << (n.AnimAutoPlay ? "true" : "false") << "\n";
        }
        if (n.HasLuaScript)
        {
            f << "has_lua = true\n";
            f << "lua_path = " << n.LuaScriptPath << "\n";
            for (const auto& kv : n.LuaExposedVars)
                f << "lua_var = " << kv.first << "|" << kv.second << "\n";
        }
        if (n.HasAudioSource)
        {
            f << "has_audio_source = true\n";
            f << "audio_clip = " << n.AudioClipPath << "\n";
            f << "audio_loop = " << (n.AudioLoop ? "true" : "false") << "\n";
            f << "audio_play_on_start = " << (n.AudioPlayOnStart ? "true" : "false") << "\n";
            f << "audio_spatial = " << (n.AudioSpatial ? "true" : "false") << "\n";
            f << "audio_volume = " << n.AudioVolume << "\n";
            f << "audio_pitch = " << n.AudioPitch << "\n";
            f << "audio_min_distance = " << n.AudioMinDistance << "\n";
            f << "audio_max_distance = " << n.AudioMaxDistance << "\n";
            f << "audio_occ_factor = " << n.AudioOcclusionFactor << "\n";
            f << "audio_output_channel = " << n.AudioOutputChannel << "\n";
        }
        if (n.HasAudioBarrier)
        {
            f << "has_audio_barrier = true\n";
            f << "audio_barrier_size = " << n.AudioBarrierSize.x << " " << n.AudioBarrierSize.y << " " << n.AudioBarrierSize.z << "\n";
            f << "audio_barrier_offset = " << n.AudioBarrierOffset.x << " " << n.AudioBarrierOffset.y << " " << n.AudioBarrierOffset.z << "\n";
            f << "audio_barrier_occ = " << n.AudioBarrierOcclusion << "\n";
        }
        if (n.HasMpManager)
        {
            f << "has_mp_manager = true\n";
            f << "mm_enabled = " << (n.MpMgrEnabled ? "true" : "false") << "\n";
            f << "mm_mode = " << n.MpMgrMode << "\n";
            f << "mm_bind = " << n.MpMgrBind << "\n";
            f << "mm_server = " << n.MpMgrServer << "\n";
            f << "mm_port = " << n.MpMgrPort << "\n";
            f << "mm_max_clients = " << n.MpMgrMaxClients << "\n";
            f << "mm_snapshot_rate = " << n.MpMgrSnapshotRate << "\n";
            f << "mm_replicate_channels = " << (n.MpMgrReplicateChannels ? "true" : "false") << "\n";
            f << "mm_auto_start = " << (n.MpMgrAutoStart ? "true" : "false") << "\n";
            f << "mm_auto_reconnect = " << (n.MpMgrAutoReconnect ? "true" : "false") << "\n";
            f << "mm_reconnect_delay = " << n.MpMgrReconnectDelay << "\n";
        }
        if (n.HasMpController)
        {
            f << "has_mp_controller = true\n";
            f << "mc_enabled = " << (n.MpCtrlEnabled ? "true" : "false") << "\n";
            f << "mc_nickname = " << n.MpCtrlNickname << "\n";
            f << "mc_network_id = " << n.MpCtrlNetworkId << "\n";
            f << "mc_is_local = " << (n.MpCtrlIsLocal ? "true" : "false") << "\n";
            f << "mc_sync_transform = " << (n.MpCtrlSyncTransform ? "true" : "false") << "\n";
        }
        if (n.HasAnimController)
        {
            f << "has_anim_controller = true\n";
            f << "ac_enabled = " << (n.AcEnabled ? "true" : "false") << "\n";
            f << "ac_auto_start = " << (n.AcAutoStart ? "true" : "false") << "\n";
            f << "ac_default = " << n.AcDefaultState << "\n";
            for (const auto& st : n.AcStates)
            {
                f << "ac_state = " << st.Name << "|"
                  << (int)st.Property << "|"
                  << (int)st.Mode << "|"
                  << (int)st.Easing << "|"
                  << st.From.x << " " << st.From.y << " " << st.From.z << "|"
                  << st.To.x   << " " << st.To.y   << " " << st.To.z   << "|"
                  << st.Duration << "|"
                  << st.Delay    << "|"
                  << st.PlaybackSpeed << "|"
                  << st.BlendWeight   << "|"
                  << (st.AutoPlay ? "true" : "false") << "\n";
            }
            for (const auto& tr : n.AcTransitions)
            {
                f << "ac_transition = " << tr.FromState << "|"
                  << tr.ToState << "|"
                  << tr.Channel << "|"
                  << (int)tr.Condition << "|"
                  << tr.Threshold << "|"
                  << tr.ExitTime << "\n";
            }
        }
        f << "\n";
    }
}

// ----------------------------------------------------------------
// Load  – reads [node] sections and fills p.Nodes
// ----------------------------------------------------------------
bool PrefabSerializer::Load(PrefabAsset& p, const std::string& filepath)
{
    std::ifstream f(filepath);
    if (!f.is_open())
    {
        std::cerr << "[PrefabSerializer] Cannot open: " << filepath << "\n";
        return false;
    }

    p.FilePath = filepath;
    p.Nodes.clear();
    PrefabEntityData cur;
    bool inNode = false;

    auto flushNode = [&]()
    {
        if (inNode) p.Nodes.push_back(cur);
        cur = PrefabEntityData{};
        inNode = false;
    };

    std::string line;
    while (std::getline(f, line))
    {
        line = ptrim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[node]") { flushNode(); inNode = true; continue; }

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = ptrim(line.substr(0, eq));
        std::string v = ptrim(line.substr(eq + 1));

        if (!inNode)
        {
            if (k == "name") p.Name = v;
            continue;
        }

        // Node-level keys
        if      (k == "node_name")     cur.Name         = v;
        else if (k == "node_parent")   cur.ParentIdx    = std::stoi(v);
        else if (k == "node_guid")     cur.NodeGuid     = std::stoi(v);
        else if (k == "node_mesh")     cur.MeshType     = v;
        else if (k == "node_material") cur.MaterialName = v;
        else if (k == "position")      cur.Transform.Position = pParseVec3(v);
        else if (k == "rotation")      cur.Transform.Rotation = pParseVec3(v);
        else if (k == "scale")         cur.Transform.Scale    = pParseVec3(v);
        else if (k == "entity_static")     cur.EntityStatic     = (v == "true");
        else if (k == "receive_lightmap")  cur.ReceiveLightmap  = (v == "true");
        else if (k == "entity_tag")        cur.Tag              = v;
        else if (k == "has_light")     cur.HasLight           = (v == "true");
        else if (k == "light_type")
        {
            if      (v == "point") cur.Light.Type = LightType::Point;
            else if (v == "spot")  cur.Light.Type = LightType::Spot;
            else if (v == "area")  cur.Light.Type = LightType::Area;
            else                   cur.Light.Type = LightType::Directional;
        }
        else if (k == "light_color")     cur.Light.Color       = pParseVec3(v);
        else if (k == "light_temp")      cur.Light.Temperature = std::stof(v);
        else if (k == "light_intensity") cur.Light.Intensity   = std::stof(v);
        else if (k == "light_range")     cur.Light.Range       = std::stof(v);
        else if (k == "light_inner")     cur.Light.InnerAngle  = std::stof(v);
        else if (k == "light_outer")     cur.Light.OuterAngle  = std::stof(v);
        else if (k == "light_width")     cur.Light.Width       = std::stof(v);
        else if (k == "light_height")    cur.Light.Height      = std::stof(v);
        else if (k == "has_camera")      cur.HasCamera         = (v == "true");
        else if (k == "cam_fov")         cur.Camera.FOV        = std::stof(v);
        else if (k == "cam_near")        cur.Camera.Near       = std::stof(v);
        else if (k == "cam_far")         cur.Camera.Far        = std::stof(v);
        else if (k == "cam_yaw")         cur.Camera.Yaw        = std::stof(v);
        else if (k == "cam_pitch")       cur.Camera.Pitch      = std::stof(v);
        else if (k == "has_trigger")     cur.HasTrigger        = (v == "true");
        else if (k == "trig_size")       cur.TriggerSize       = pParseVec3(v);
        else if (k == "trig_offset")     cur.TriggerOffset     = pParseVec3(v);
        else if (k == "trig_channel")    cur.TriggerChannel    = std::stoi(v);
        else if (k == "trig_inside")     cur.TriggerInsideValue = (v == "true");
        else if (k == "trig_outside")    cur.TriggerOutsideValue = (v == "true");
        else if (k == "trig_oneshot")    cur.TriggerOneShot    = (v == "true");
        else if (k == "has_animator")    cur.HasAnimator       = (v == "true");
        else if (k == "anim_prop")       cur.AnimProp          = std::stoi(v);
        else if (k == "anim_mode")       cur.AnimMode          = std::stoi(v);
        else if (k == "anim_ease")       cur.AnimEasing        = std::stoi(v);
        else if (k == "anim_from")       cur.AnimFrom          = pParseVec3(v);
        else if (k == "anim_to")         cur.AnimTo            = pParseVec3(v);
        else if (k == "anim_duration")   cur.AnimDuration      = std::stof(v);
        else if (k == "anim_delay")      cur.AnimDelay         = std::stof(v);
        else if (k == "anim_speed")      cur.AnimSpeed         = std::stof(v);
        else if (k == "anim_blend")      cur.AnimBlend         = std::stof(v);
        else if (k == "anim_auto")       cur.AnimAutoPlay      = (v == "true");
        else if (k == "has_lua")         cur.HasLuaScript      = (v == "true");
        else if (k == "lua_path")        cur.LuaScriptPath     = v;
        else if (k == "lua_var")
        {
            auto pipe = v.find('|');
            if (pipe != std::string::npos)
                cur.LuaExposedVars[v.substr(0, pipe)] = v.substr(pipe + 1);
        }
        else if (k == "has_audio_source") cur.HasAudioSource   = (v == "true");
        else if (k == "audio_clip")       cur.AudioClipPath    = v;
        else if (k == "audio_loop")       cur.AudioLoop        = (v == "true");
        else if (k == "audio_play_on_start") cur.AudioPlayOnStart = (v == "true");
        else if (k == "audio_spatial")    cur.AudioSpatial     = (v == "true");
        else if (k == "audio_volume")     cur.AudioVolume      = std::stof(v);
        else if (k == "audio_pitch")      cur.AudioPitch       = std::stof(v);
        else if (k == "audio_min_distance") cur.AudioMinDistance = std::stof(v);
        else if (k == "audio_max_distance") cur.AudioMaxDistance = std::stof(v);
        else if (k == "audio_occ_factor") cur.AudioOcclusionFactor = std::stof(v);
        else if (k == "audio_output_channel") cur.AudioOutputChannel = std::stoi(v);
        else if (k == "has_audio_barrier") cur.HasAudioBarrier = (v == "true");
        else if (k == "audio_barrier_size") cur.AudioBarrierSize = pParseVec3(v);
        else if (k == "audio_barrier_offset") cur.AudioBarrierOffset = pParseVec3(v);
        else if (k == "audio_barrier_occ") cur.AudioBarrierOcclusion = std::stof(v);
        else if (k == "has_mp_manager")    cur.HasMpManager = (v == "true");
        else if (k == "mm_enabled")        cur.MpMgrEnabled = (v == "true");
        else if (k == "mm_mode")           cur.MpMgrMode    = std::stoi(v);
        else if (k == "mm_bind")           cur.MpMgrBind    = v;
        else if (k == "mm_server")         cur.MpMgrServer  = v;
        else if (k == "mm_port")           cur.MpMgrPort    = std::stoi(v);
        else if (k == "mm_max_clients")    cur.MpMgrMaxClients = std::stoi(v);
        else if (k == "mm_snapshot_rate")  cur.MpMgrSnapshotRate = std::stof(v);
        else if (k == "mm_replicate_channels") cur.MpMgrReplicateChannels = (v == "true");
        else if (k == "mm_auto_start")     cur.MpMgrAutoStart = (v == "true");
        else if (k == "mm_auto_reconnect") cur.MpMgrAutoReconnect = (v == "true");
        else if (k == "mm_reconnect_delay") cur.MpMgrReconnectDelay = std::stof(v);
        else if (k == "has_mp_controller") cur.HasMpController = (v == "true");
        else if (k == "mc_enabled")        cur.MpCtrlEnabled = (v == "true");
        else if (k == "mc_nickname")       cur.MpCtrlNickname = v;
        else if (k == "mc_network_id")     cur.MpCtrlNetworkId = (uint64_t)std::stoull(v);
        else if (k == "mc_is_local")       cur.MpCtrlIsLocal = (v == "true");
        else if (k == "mc_sync_transform") cur.MpCtrlSyncTransform = (v == "true");
        else if (k == "has_anim_controller") cur.HasAnimController = (v == "true");
        else if (k == "ac_enabled")        cur.AcEnabled = (v == "true");
        else if (k == "ac_auto_start")     cur.AcAutoStart = (v == "true");
        else if (k == "ac_default")        cur.AcDefaultState = std::stoi(v);
        else if (k == "ac_state")
        {
            AnimationStateData st;
            std::istringstream ss(v);
            std::string token;
            auto next = [&](std::string& out){ if (!std::getline(ss, out, '|')) out=""; };
            next(st.Name);
            next(token); st.Property = (AnimatorProperty)std::stoi(token);
            next(token); st.Mode     = (AnimatorMode)std::stoi(token);
            next(token); st.Easing   = (AnimatorEasing)std::stoi(token);
            next(token); { std::istringstream vs(token); vs >> st.From.x >> st.From.y >> st.From.z; }
            next(token); { std::istringstream vs(token); vs >> st.To.x   >> st.To.y   >> st.To.z; }
            next(token); st.Duration = std::stof(token);
            next(token); st.Delay    = std::stof(token);
            next(token); st.PlaybackSpeed = std::stof(token);
            next(token); st.BlendWeight   = std::stof(token);
            next(token); st.AutoPlay = (token == "true");
            cur.AcStates.push_back(st);
        }
        else if (k == "ac_transition")
        {
            AnimationTransitionData tr;
            std::istringstream ss(v);
            std::string token;
            auto next = [&](std::string& out){ if (!std::getline(ss, out, '|')) out=""; };
            next(token); tr.FromState = std::stoi(token);
            next(token); tr.ToState   = std::stoi(token);
            next(token); tr.Channel   = std::stoi(token);
            next(token); tr.Condition = (AnimationConditionOp)std::stoi(token);
            next(token); tr.Threshold = std::stof(token);
            next(token); tr.ExitTime  = std::stof(token);
            cur.AcTransitions.push_back(tr);
        }
    }
    flushNode(); // flush the last node
    for (int i = 0; i < (int)p.Nodes.size(); ++i)
        if (p.Nodes[i].NodeGuid < 0) p.Nodes[i].NodeGuid = i;
    return !p.Nodes.empty();
}

} // namespace tsu
