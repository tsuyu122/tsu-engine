#include "core/application.h"
#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "renderer/lightBaker.h"
#include "renderer/lightmapManager.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include "procedural/mazeGenerator.h"
#include "lua/luaSystem.h"
#include "network/multiplayerSystem.h"
#include "scene/entity.h"
#include "serialization/sceneSerializer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tsu {

// Buffer of files dragged from outside the editor (GLFW drop callback)
static std::vector<std::string> s_DroppedFiles;

static void OnGLFWDrop(GLFWwindow*, int count, const char** paths)
{
    for (int i = 0; i < count; ++i)
        s_DroppedFiles.emplace_back(paths[i]);
}

static std::string MakeUniqueEntityName(const Scene& scene, const std::string& base)
{
    auto exists = [&](const std::string& n) -> bool {
        for (const auto& e : scene.EntityNames)
            if (e == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 2; i < 10000; ++i)
    {
        std::string candidate = base + " (" + std::to_string(i) + ")";
        if (!exists(candidate)) return candidate;
    }
    return base;
}

#ifdef _WIN32
static bool LaunchGameRuntimeProcess(const std::string& cmdLine)
{
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmdLine;
    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif

static void FreeSceneMeshes(Scene& scene)
{
    for (auto& mr : scene.MeshRenderers)
    {
        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
    }
}

Application::Application()
    : m_Window(1280, 720, "TSU Engine  |  ALPHA 1.3.1 - NOT RELEASE VERSION, EXPECT BUGS")
{
}

void Application::StartSceneBakeAsync()
{
    if (m_BakeRunning.load()) return;

    m_BakeError.clear();
    m_BakeReady = false;
    m_BakeProgress.store(0.0f);
    {
        std::lock_guard<std::mutex> lk(m_BakeMutex);
        m_BakeStage = "Preparing bake";
    }

    // UV2 generation touches GL buffers, so keep it on the main thread.
    int generatedUV2 = 0;
    for (int i = 0; i < (int)m_Scene.MeshRenderers.size(); ++i) {
        auto& mr = m_Scene.MeshRenderers[i];
        if (mr.MeshPtr) {
            mr.MeshPtr->GenerateAutoUV2();
            generatedUV2++;
        }
    }
    if (generatedUV2 > 0)
        m_UIManager.Log("[LightBaker] Rebuilt UV2 for " + std::to_string(generatedUV2) + " mesh(es)");

    Scene sceneCopy = m_Scene;
    for (int i = 0; i < (int)sceneCopy.MeshRenderers.size(); ++i) {
        Mesh* src = m_Scene.MeshRenderers[i].MeshPtr;
        sceneCopy.MeshRenderers[i].MeshPtr = src ? new Mesh(*src) : nullptr;
    }
    auto lights = LightBaker::ExtractLights(sceneCopy);
    auto params = m_UIManager.GetBakeParams();
    params.resolution      = std::max(32, std::min(4096, params.resolution));
    params.aoSamples       = std::max(4, params.aoSamples);
    params.indirectSamples = std::max(8, params.indirectSamples);
    params.bounceCount     = std::max(1, std::min(8, params.bounceCount));
    params.aoRadius        = std::max(0.01f, params.aoRadius);
    params.ambientLevel    = std::max(0.0f, std::min(1.0f, params.ambientLevel));
    params.directScale     = std::max(0.0f, params.directScale);
    params.denoiseRadius   = std::max(0, std::min(8, params.denoiseRadius));

    m_BakeRunning.store(true);
    if (m_BakeThread.joinable()) m_BakeThread.join();

    m_BakeThread = std::thread([this, sceneCopy, lights, params]() mutable {
        try {
            auto result = LightBaker::BakeSceneAtlasUV2(
                sceneCopy,
                lights,
                params,
                [this](float p, const std::string& stage) {
                    m_BakeProgress.store(p);
                    std::lock_guard<std::mutex> lk(m_BakeMutex);
                    m_BakeStage = stage;
                });

            if (result.pixels.empty()) {
                std::lock_guard<std::mutex> lk(m_BakeMutex);
                m_BakeError = "Error: bake returned empty atlas";
            } else {
                std::lock_guard<std::mutex> lk(m_BakeMutex);
                m_BakeResult = std::move(result);
                m_BakeReady = true;
            }
        } catch (const std::exception& ex) {
            std::lock_guard<std::mutex> lk(m_BakeMutex);
            m_BakeError = std::string("Error: ") + ex.what();
        } catch (...) {
            std::lock_guard<std::mutex> lk(m_BakeMutex);
            m_BakeError = "Error: unknown bake failure";
        }

        for (auto& mr : sceneCopy.MeshRenderers)
            if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }

        m_BakeProgress.store(1.0f);
        m_BakeRunning.store(false);
    });
}

void Application::PollSceneBakeAsync()
{
    std::string stage;
    {
        std::lock_guard<std::mutex> lk(m_BakeMutex);
        stage = m_BakeStage;
    }
    m_UIManager.SetBakeProgress(m_BakeRunning.load(), m_BakeProgress.load(), stage);

    if (m_BakeRunning.load()) return;

    if (m_BakeThread.joinable())
        m_BakeThread.join();

    std::string bakeError;
    {
        std::lock_guard<std::mutex> lk(m_BakeMutex);
        bakeError = m_BakeError;
        m_BakeError.clear();
    }
    if (!bakeError.empty()) {
        m_UIManager.SetBakeProgress(false, 0.0f, "");
        m_UIManager.SetBakeStatus(bakeError);
        m_UIManager.Log("[LightBaker] " + bakeError);
        m_BakeReady = false;
        return;
    }

    LightBaker::AtlasBakeResult result;
    {
        std::lock_guard<std::mutex> lk(m_BakeMutex);
        if (!m_BakeReady) return;
        result = m_BakeResult;
        m_BakeReady = false;
    }

    std::string outDir = "assets/lightmaps";
    std::filesystem::create_directories(outDir);
    std::string outPath = outDir + "/scene_atlas_0.tga";

    bool saved = LightmapManager::SaveTGA(outPath, result.width, result.height, result.pixels.data());
    if (!saved) {
        m_UIManager.SetBakeProgress(false, 0.0f, "");
        m_UIManager.SetBakeStatus("Error: failed to save atlas TGA");
        m_UIManager.Log("[LightBaker] Error saving scene atlas TGA");
        return;
    }

    LightmapManager::Instance().Evict(outPath);

    // Upload linear HDR float data directly as GL_RGB16F (no gamma encode/decode).
    // The TGA is still saved (gamma-encoded) for debug inspection on disk.
    unsigned int lmID = 0;
    if (!result.hdrPixels.empty()) {
        lmID = LightmapManager::LoadFromFloat(result.width, result.height, result.hdrPixels.data());
    }
    if (lmID == 0) {
        // Fallback: load from TGA if float path unavailable
        lmID = LightmapManager::Instance().Load(outPath);
    }
    if (lmID == 0) {
        m_UIManager.SetBakeProgress(false, 0.0f, "");
        m_UIManager.SetBakeStatus("Error: failed to load atlas texture");
        return;
    }

    if (!m_SceneLightmapPath.empty())
        LightmapManager::Instance().Release(m_SceneLightmapPath);
    m_SceneLightmapPath = outPath;
    m_UIManager.SetSceneLightmapPath(outPath);
    Renderer::ClearGlobalLightmaps();
    Renderer::SetGlobalLightmap(0, lmID);
    // Use the HDR peak from the bake so the normalized 0-1 lightmap values
    // are scaled back to their original absolute irradiance at runtime.
    float targetIntensity = std::max(result.hdrMax, 0.01f);
    if (m_Scene.BakedLighting.AutoApplyLightmapIntensity)
    {
        targetIntensity *= std::max(0.0f, m_Scene.BakedLighting.AutoIntensityMultiplier);
        targetIntensity = std::min(targetIntensity, std::max(0.01f, m_Scene.BakedLighting.MaxAutoIntensity));
    }
    m_Scene.LightmapIntensity = targetIntensity;
    m_UIManager.Log("[LightBaker] === BAKE v2-diag ===");
    m_UIManager.Log("[LightBaker] Atlas: " + std::to_string(result.width) + "x" + std::to_string(result.height)
                  + ", hdrMax=" + std::to_string(result.hdrMax)
                  + ", LightmapIntensity=" + std::to_string(m_Scene.LightmapIntensity)
                  + ", texID=" + std::to_string((int)lmID));

    std::vector<glm::vec4> soByEntity(m_Scene.MeshRenderers.size(), glm::vec4(0,0,0,0));
    std::vector<int> hasAssign(m_Scene.MeshRenderers.size(), 0);
    int assignedCount = 0;
    for (const auto& a : result.assignments) {
        if (a.entity < 0 || a.entity >= (int)m_Scene.MeshRenderers.size()) continue;
        soByEntity[a.entity] = a.scaleOffset;
        hasAssign[a.entity] = 1;
        assignedCount++;
    }
    m_UIManager.Log("[LightBaker] Atlas assignments: " + std::to_string(assignedCount));

    int appliedCount = 0;

    for (int ei = 0; ei < (int)m_Scene.MeshRenderers.size(); ++ei)
    {
        auto& mr = m_Scene.MeshRenderers[ei];
        if (mr.MeshPtr == nullptr) continue;
        bool isStatic = (ei < (int)m_Scene.EntityStatic.size()) ? m_Scene.EntityStatic[ei] : true;
        bool receiveLM = (ei < (int)m_Scene.EntityReceiveLightmap.size()) ? m_Scene.EntityReceiveLightmap[ei] : true;
        if (!isStatic || !receiveLM || !hasAssign[ei]) {
            mr.LightmapID = 0;
            mr.LightmapIndex = -1;
            mr.LightmapST = {1,1,0,0};
            continue;
        }

        mr.LightmapID = lmID;
        mr.LightmapIndex = 0;
        // LightmapManager now loads with stbi flip=false, so baker UV maps
        // directly to GL UV y without any Y inversion.
        mr.LightmapST = soByEntity[ei];

        mr.LightmapIDX = 0;
        mr.LightmapIDY = 0;
        mr.LightmapIDZ = 0;
        mr.LightmapSTX = {1,1,0,0};
        mr.LightmapSTY = {1,1,0,0};
        mr.LightmapSTZ = {1,1,0,0};
        appliedCount++;

        const std::string name = (ei < (int)m_Scene.EntityNames.size()) ? m_Scene.EntityNames[ei] : ("Entity_" + std::to_string(ei));
        m_UIManager.Log("[LightBaker] Apply -> " + name + " idx=0 tex=" + std::to_string((int)lmID) + " so=("
            + std::to_string(soByEntity[ei].x) + ","
            + std::to_string(soByEntity[ei].y) + ","
            + std::to_string(soByEntity[ei].z) + ","
            + std::to_string(soByEntity[ei].w) + ") soGL=("
            + std::to_string(mr.LightmapST.x) + ","
            + std::to_string(mr.LightmapST.y) + ","
            + std::to_string(mr.LightmapST.z) + ","
            + std::to_string(mr.LightmapST.w) + ")");
    }

    std::string metaPath = outDir + "/scene_lightmap_data.json";
    std::ofstream meta(metaPath, std::ios::trunc);
    if (meta.is_open()) {
        meta << "{\n  \"lightmaps\": [{\"index\": 0, \"path\": \"" << outPath << "\"}],\n";
        meta << "  \"meshes\": [\n";
        bool first = true;
        for (int ei = 0; ei < (int)m_Scene.MeshRenderers.size(); ++ei) {
            if (!hasAssign[ei]) continue;
            if (!first) meta << ",\n";
            first = false;
            const std::string name = (ei < (int)m_Scene.EntityNames.size()) ? m_Scene.EntityNames[ei] : ("Entity_" + std::to_string(ei));
            const glm::vec4 so = m_Scene.MeshRenderers[ei].LightmapST;
            meta << "    {\"meshID\": \"" << name << "\", \"lightmapIndex\": 0, \"path\": \"" << outPath << "\", \"scaleOffset\": ["
                 << so.x << ", " << so.y << ", " << so.z << ", " << so.w << "]}";
        }
        meta << "\n  ]\n}\n";
    }

    m_UIManager.Log("[LightBaker] Applied atlas to " + std::to_string(appliedCount) + " renderer(s)");
    m_UIManager.SetBakeProgress(false, 0.0f, "");
    m_UIManager.SetBakeStatus("Baked UV2 Atlas -> assets/lightmaps/scene_atlas_0.tga");
    m_UIManager.Log("[LightBaker] UV2 atlas bake complete: " + outPath);
}

void Application::SaveEditorState()
{
    m_Snapshot.clear();
    for (size_t i=0; i<m_Scene.Transforms.size(); i++)
        m_Snapshot.push_back({m_Scene.Transforms[i].Position,
                               m_Scene.RigidBodies[i].Velocity,
                               m_Scene.Transforms[i].Rotation,
                               m_Scene.RigidBodies[i].AngularVelocity});
}

void Application::SpawnMultiplayerPlayers()
{
    m_SpawnedMpPlayerIds.clear();

    for (int mmi = 0; mmi < (int)m_Scene.MultiplayerManagers.size(); ++mmi)
    {
        auto& mm = m_Scene.MultiplayerManagers[mmi];
        if (!mm.Active || !mm.Enabled) continue;
        if (mm.PlayerPrefabIdx < 0 || mm.PlayerPrefabIdx >= (int)m_Scene.Prefabs.size()) continue;

        const std::string& prefabName = m_Scene.Prefabs[mm.PlayerPrefabIdx].Name;
        bool isHost = (mm.Mode == MultiplayerMode::Host);

        // Spawn two player slots from the prefab so both sides have the same entity indices.
        // Slot 0 = Player1 (owned by host), Slot 1 = Player2 (owned by client).
        glm::vec3 spawnPos0 = mm.SpawnPoint + glm::vec3(-1.5f, 0.0f, 0.0f);
        glm::vec3 spawnPos1 = mm.SpawnPoint + glm::vec3( 1.5f, 0.0f, 0.0f);

        int id0 = m_Scene.SpawnFromPrefab(mm.PlayerPrefabIdx, spawnPos0);
        int id1 = m_Scene.SpawnFromPrefab(mm.PlayerPrefabIdx, spawnPos1);

        auto setupSlot = [&](int id, bool isLocal, const std::string& nick)
        {
            if (id < 0) return;
            m_SpawnedMpPlayerIds.push_back(id);

            // MultiplayerController
            auto& mc = m_Scene.MultiplayerControllers[id];
            mc.Active        = true;
            mc.IsLocalPlayer = isLocal;
            mc.SyncTransform = true;
            mc.Nickname      = nick;
            if (mc.NetworkId == 0)
                mc.NetworkId = MultiplayerSystem::MakeNetworkId(nick);

            // PlayerController — only the local player responds to keyboard input
            if (isLocal)
            {
                auto& pc = m_Scene.PlayerControllers[id];
                pc.Active      = true;
                pc.Enabled     = true;
                pc.MovementMode = PlayerMovementMode::LocalWithCollision;
            }
        };

        setupSlot(id0, isHost,  "Player1");
        setupSlot(id1, !isHost, "Player2");

        m_UIManager.Log(std::string("[MP] Player prefab '") + prefabName +
                        "' spawned — local slot=" + (isHost ? "0" : "1"));
        break; // only process first active MM
    }
}

void Application::DespawnMultiplayerPlayers()
{
    // Ask the network to shut down, then tick it once to process the stop.
    for (auto& mm : m_Scene.MultiplayerManagers)
    {
        if (mm.Active && mm.Running)
            mm.RequestStop = true;
    }
    MultiplayerSystem::Update(m_Scene, 0.0f);
    m_SpawnedMpPlayerIds.clear();
    // Spawned entities are removed by RestoreEditorState() (it trims to pre-play count).
}

void Application::RestoreEditorState()
{
    // Delete entities that were spawned during play (beyond the snapshot count)
    int snapshotCount = (int)m_Snapshot.size();
    for (int i = (int)m_Scene.Transforms.size() - 1; i >= snapshotCount; --i)
        m_Scene.DeleteEntity(i);

    // Restore original transforms/physics for the pre-play entities
    for (size_t i = 0; i < m_Snapshot.size() && i < m_Scene.Transforms.size(); i++)
    {
        m_Scene.Transforms[i].Position          = m_Snapshot[i].position;
        m_Scene.RigidBodies[i].Velocity         = m_Snapshot[i].velocity;
        m_Scene.Transforms[i].Rotation          = m_Snapshot[i].rotation;
        m_Scene.RigidBodies[i].AngularVelocity  = m_Snapshot[i].angularVelocity;
        m_Scene.RigidBodies[i].IsGrounded       = false;
    }

    // Clear maze generator runtime state (spawned rooms + occupancy + pending doors)
    for (auto& gen : m_Scene.MazeGenerators)
    {
        gen.SpawnedRooms.clear();
        gen.OccupiedCells.clear();
        gen.PendingDoorSlots.clear();
    }
}

// ============================================================
// Project management
// ============================================================

static std::string TrimStr(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

void Application::NewProject(const std::string& name, const std::string& parentFolder)
{
    namespace fs = std::filesystem;
    std::string dir = (parentFolder.empty() ? "." : parentFolder) + "/" + name;
    try {
        fs::create_directories(dir + "/assets/scenes");
        fs::create_directories(dir + "/assets/scripts");
        fs::create_directories(dir + "/assets/prefabs");

        // Write .tsproj descriptor
        {
            std::ofstream f(dir + "/" + name + ".tsproj");
            f << "# tsuEngine Project File\n"
              << "name  = " << name << "\n"
              << "scene = assets/scenes/main.tscene\n";
        }

        // Free existing mesh pointers before clearing the scene
        for (auto& mr : m_Scene.MeshRenderers)
            if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }

        // Create default empty scene
        m_Scene         = Scene{};
        m_SelectedEntity = -1;
        SceneSerializer::Save(m_Scene, dir + "/assets/scenes/main.tscene");

        m_ProjectName        = name;
        m_ProjectFolder      = fs::absolute(dir).string();
        m_CurrentScenePath   = fs::absolute(dir + "/assets/scenes/main.tscene").string();
        m_UIManager.SetProjectDisplayName(m_ProjectName);
        RefreshSceneFiles();
        m_UIManager.Log("[Project] New project '" + name + "' created at: " + m_ProjectFolder);
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Project] Error creating project: ") + ex.what());
    }
}

void Application::OpenProject(const std::string& inputPath)
{
    namespace fs = std::filesystem;
    fs::path p(inputPath);
    std::string projFolder;
    std::string projName;
    std::string scenePath;
    std::string currentScenePath;

    try {
        if (p.extension() == ".tsproj") {
            projFolder = p.parent_path().string();
            projName   = p.stem().string();
            scenePath  = projFolder + "/assets/scenes/main.tscene";
            // Parse .tsproj for overrides
            std::ifstream f(inputPath);
            std::string line;
            while (std::getline(f, line)) {
                auto e = line.find('=');
                if (e == std::string::npos) continue;
                std::string k = TrimStr(line.substr(0, e));
                std::string v = TrimStr(line.substr(e + 1));
                if (k == "name")          projName         = v;
                if (k == "scene")         scenePath        = projFolder + "/" + v;
                if (k == "current_scene") currentScenePath = projFolder + "/" + v;
            }
        } else {
            // Treat as project folder
            projFolder = inputPath;
            for (auto& entry : fs::directory_iterator(inputPath)) {
                if (entry.path().extension() == ".tsproj") {
                    projName = entry.path().stem().string();
                    break;
                }
            }
            if (projName.empty()) projName = p.filename().string();
            scenePath = projFolder + "/assets/scenes/main.tscene";
        }

        // If the editor remembered which scene was open, use that; otherwise use startup scene
        std::string loadPath = (!currentScenePath.empty() && fs::exists(currentScenePath))
                               ? currentScenePath : scenePath;

        m_ProjectName    = projName;
        m_ProjectFolder  = fs::absolute(projFolder).string();

        // Free existing mesh pointers before clearing the scene
        for (auto& mr : m_Scene.MeshRenderers)
            if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }

        m_Scene          = Scene{};
        m_SelectedEntity = -1;

        if (fs::exists(loadPath)) {
            SceneSerializer::Load(m_Scene, loadPath);
            m_EditorCamera.SetPosition(m_Scene.EditorCamPos);
            m_EditorCamera.SetYaw(m_Scene.EditorCamYaw);
            m_EditorCamera.SetPitch(m_Scene.EditorCamPitch);
            m_CurrentScenePath = fs::absolute(loadPath).string();
        } else {
            m_UIManager.Log("[Project] Warning: scene not found: " + loadPath);
            m_CurrentScenePath = fs::absolute(scenePath).string();
        }

        m_UIManager.SetProjectDisplayName(m_ProjectName);
        RefreshSceneFiles();
        m_UIManager.Log("[Project] Opened: " + m_ProjectFolder);
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Project] Error opening project: ") + ex.what());
    }
}

void Application::SaveProject()
{
    namespace fs = std::filesystem;
    try {
        if (m_ProjectFolder.empty()) {
            // No project set — save current scene to the default location
            fs::create_directories("assets/scenes");
            m_Scene.EditorCamPos   = m_EditorCamera.GetPosition();
            m_Scene.EditorCamYaw   = m_EditorCamera.GetYaw();
            m_Scene.EditorCamPitch = m_EditorCamera.GetPitch();
            SceneSerializer::Save(m_Scene, "assets/scenes/default.tscene");
            m_UIManager.Log("[Project] Saved scene to assets/scenes/default.tscene");
            return;
        }

        fs::create_directories(m_ProjectFolder + "/assets/scenes");
        fs::create_directories(m_ProjectFolder + "/assets/scripts");

        // Save current scene (to whichever scene is open, default to main.tscene)
        std::string savePath = m_CurrentScenePath.empty()
                               ? m_ProjectFolder + "/assets/scenes/main.tscene"
                               : m_CurrentScenePath;
        m_Scene.EditorCamPos   = m_EditorCamera.GetPosition();
        m_Scene.EditorCamYaw   = m_EditorCamera.GetYaw();
        m_Scene.EditorCamPitch = m_EditorCamera.GetPitch();
        SceneSerializer::Save(m_Scene, savePath);

        // Copy assets directory (scripts, textures, meshes, prefabs…) if it exists
        if (fs::exists("assets")) {
            fs::copy("assets", m_ProjectFolder + "/assets",
                     fs::copy_options::recursive | fs::copy_options::update_existing);
        }

        // Compute relative paths for .tsproj
        auto toRelative = [&](const std::string& absPath) -> std::string {
            try {
                return fs::relative(fs::path(absPath), fs::path(m_ProjectFolder)).string();
            } catch (...) { return "assets/scenes/main.tscene"; }
        };

        // The startup scene (scene =) stays as main.tscene unless changed explicitly in future
        // current_scene = remembers which scene the editor had open
        std::string relCurrent = m_CurrentScenePath.empty()
                                 ? "assets/scenes/main.tscene"
                                 : toRelative(m_CurrentScenePath);

        // Write/refresh .tsproj
        {
            std::ofstream f(m_ProjectFolder + "/" + m_ProjectName + ".tsproj");
            f << "# tsuEngine Project File\n"
              << "name          = " << m_ProjectName << "\n"
              << "scene         = assets/scenes/main.tscene\n"
              << "current_scene = " << relCurrent << "\n";
        }

        RefreshSceneFiles();
        m_UIManager.Log("[Project] Project saved: " + m_ProjectFolder);
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Project] Error saving: ") + ex.what());
    }
}

void Application::NewScene(const std::string& name)
{
    namespace fs = std::filesystem;

    // Resolve the base scenes directory
    std::string scenesDir = !m_ProjectFolder.empty()
                            ? m_ProjectFolder + "/assets/scenes"
                            : "assets/scenes";
    fs::create_directories(scenesDir);

    // Auto-save current scene before switching
    if (!m_CurrentScenePath.empty()) {
        m_Scene.EditorCamPos   = m_EditorCamera.GetPosition();
        m_Scene.EditorCamYaw   = m_EditorCamera.GetYaw();
        m_Scene.EditorCamPitch = m_EditorCamera.GetPitch();
        SceneSerializer::Save(m_Scene, m_CurrentScenePath);
    }

    std::string newPath = scenesDir + "/" + name + ".tscene";
    for (auto& mr : m_Scene.MeshRenderers)
        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
    m_Scene          = Scene{};
    m_SelectedEntity = -1;
    SceneSerializer::Save(m_Scene, newPath);
    m_CurrentScenePath = fs::absolute(newPath).string();
    RefreshSceneFiles();
    m_UIManager.Log("[Scene] Created: " + newPath);
}

void Application::OpenScene(const std::string& path)
{
    namespace fs = std::filesystem;
    if (path.empty() || !fs::exists(path)) {
        m_UIManager.Log("[Scene] File not found: " + path);
        return;
    }
    if (m_Mode != EngineMode::Editor) {
        m_UIManager.Log("[Scene] Stop Play mode before switching scenes.");
        return;
    }
    // Auto-save current scene before switching
    if (!m_CurrentScenePath.empty()) {
        m_Scene.EditorCamPos   = m_EditorCamera.GetPosition();
        m_Scene.EditorCamYaw   = m_EditorCamera.GetYaw();
        m_Scene.EditorCamPitch = m_EditorCamera.GetPitch();
        SceneSerializer::Save(m_Scene, m_CurrentScenePath);
    }

    // Preserve project-level assets across scene switch
    auto savedFolders       = std::move(m_Scene.Folders);
    auto savedMaterials     = std::move(m_Scene.Materials);
    auto savedTextures      = std::move(m_Scene.Textures);
    auto savedTextureIDs    = std::move(m_Scene.TextureIDs);
    auto savedTextureFolders= std::move(m_Scene.TextureFolders);
    auto savedPrefabs       = std::move(m_Scene.Prefabs);
    auto savedMeshAssets    = std::move(m_Scene.MeshAssets);
    auto savedMeshFolders   = std::move(m_Scene.MeshAssetFolders);
    auto savedScriptAssets  = std::move(m_Scene.ScriptAssets);
    auto savedScriptFolders = std::move(m_Scene.ScriptAssetFolders);

    for (auto& mr : m_Scene.MeshRenderers)
        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
    m_Scene          = Scene{};
    m_SelectedEntity = -1;
    SceneSerializer::Load(m_Scene, path);

    // Restore project-level assets (scene file doesn't define them)
    if (m_Scene.Folders.empty())        m_Scene.Folders        = std::move(savedFolders);
    if (m_Scene.Materials.empty())      m_Scene.Materials      = std::move(savedMaterials);
    if (m_Scene.Textures.empty())       m_Scene.Textures       = std::move(savedTextures);
    if (m_Scene.TextureIDs.empty())     m_Scene.TextureIDs     = std::move(savedTextureIDs);
    if (m_Scene.TextureFolders.empty()) m_Scene.TextureFolders = std::move(savedTextureFolders);
    if (m_Scene.Prefabs.empty())        m_Scene.Prefabs        = std::move(savedPrefabs);
    if (m_Scene.MeshAssets.empty())     m_Scene.MeshAssets     = std::move(savedMeshAssets);
    if (m_Scene.MeshAssetFolders.empty()) m_Scene.MeshAssetFolders = std::move(savedMeshFolders);
    if (m_Scene.ScriptAssets.empty())   m_Scene.ScriptAssets   = std::move(savedScriptAssets);
    if (m_Scene.ScriptAssetFolders.empty()) m_Scene.ScriptAssetFolders = std::move(savedScriptFolders);

    m_EditorCamera.SetPosition(m_Scene.EditorCamPos);
    m_EditorCamera.SetYaw(m_Scene.EditorCamYaw);
    m_EditorCamera.SetPitch(m_Scene.EditorCamPitch);
    m_CurrentScenePath = fs::absolute(path).string();
    RefreshSceneFiles();
    m_UIManager.Log("[Scene] Opened: " + path);
}

void Application::DeleteScene(const std::string& path)
{
    namespace fs = std::filesystem;
    try {
        if (fs::absolute(path) == fs::path(m_CurrentScenePath)) {
            m_UIManager.Log("[Scene] Cannot delete the currently open scene.");
            return;
        }
        fs::remove(path);
        RefreshSceneFiles();
        m_UIManager.Log("[Scene] Deleted: " + path);
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Scene] Delete error: ") + ex.what());
    }
}

void Application::RefreshSceneFiles()
{
    namespace fs = std::filesystem;
    std::vector<std::string> files;

    // If a project is open, scan its scenes dir; otherwise fall back to local assets/scenes/
    std::string scenesDir = !m_ProjectFolder.empty()
                            ? m_ProjectFolder + "/assets/scenes"
                            : "assets/scenes";

    if (fs::exists(scenesDir)) {
        for (auto& entry : fs::directory_iterator(scenesDir))
            if (entry.path().extension() == ".tscene")
                files.push_back(fs::absolute(entry.path()).string());
        std::sort(files.begin(), files.end());
    }
    m_UIManager.SetSceneFiles(files, m_CurrentScenePath);

    // Refresh script assets index (recursive)
    m_Scene.ScriptAssets.clear();
    m_Scene.ScriptAssetFolders.clear();
    std::string scriptsDir = !m_ProjectFolder.empty()
                            ? m_ProjectFolder + "/assets/scripts"
                            : "assets/scripts";
    if (fs::exists(scriptsDir)) {
        for (auto& entry : fs::recursive_directory_iterator(scriptsDir))
            if (entry.is_regular_file() && entry.path().extension() == ".lua")
                m_Scene.ScriptAssets.push_back(fs::absolute(entry.path()).string());
    }

    // Refresh shader assets index (recursive)
    m_Scene.ShaderAssets.clear();
    std::string shaderDir = !m_ProjectFolder.empty()
                            ? m_ProjectFolder + "/assets/shaders"
                            : "assets/shaders";
    if (fs::exists(shaderDir)) {
        for (auto& entry : fs::recursive_directory_iterator(shaderDir))
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".glsl" || ext == ".frag" || ext == ".vert")
                    m_Scene.ShaderAssets.push_back(fs::absolute(entry.path()).string());
            }
    }

    // Refresh audio assets index (recursive)
    m_Scene.AudioAssets.clear();
    std::string audioDir = !m_ProjectFolder.empty()
                            ? m_ProjectFolder + "/assets/audio"
                            : "assets/audio";
    if (fs::exists(audioDir)) {
        for (auto& entry : fs::recursive_directory_iterator(audioDir))
            if (entry.is_regular_file()) {
                auto ext = entry.path().extension().string();
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".wav" || ext == ".ogg" || ext == ".mp3")
                    m_Scene.AudioAssets.push_back(fs::absolute(entry.path()).string());
            }
    }
}

void Application::ExportGame(const std::string& outputFolder, const std::string& gameNameOverride)
{
    namespace fs = std::filesystem;
    try {
        fs::create_directories(outputFolder);

        // 1. Find this exe
        std::string exeSrc;
#ifdef _WIN32
        char buf[4096] = {};
        GetModuleFileNameA(nullptr, buf, sizeof(buf));
        exeSrc = buf;
#endif

        std::string gameName = !gameNameOverride.empty() ? gameNameOverride
                               : (!m_ProjectName.empty() ? m_ProjectName : "Game");

        // 2. Copy GameRuntime.exe — look first in _engine/ subdirectory (hidden location),
        //    then fall back to the sibling directory for older builds.
        if (!exeSrc.empty()) {
            fs::path exeDir     = fs::path(exeSrc).parent_path();
            fs::path runtimeExe = exeDir / "_engine" / "GameRuntime.exe";
            if (!fs::exists(runtimeExe))
                runtimeExe = exeDir / "GameRuntime.exe"; // fallback: old location
            std::string srcExe  = fs::exists(runtimeExe) ? runtimeExe.string() : exeSrc;
            std::string exeDest = outputFolder + "/" + gameName + ".exe";
            fs::copy_file(srcExe, exeDest, fs::copy_options::overwrite_existing);
        }

        // 3. Save current scene as the default one the exe will load
        fs::create_directories(outputFolder + "/assets/scenes");
        SceneSerializer::Save(m_Scene, outputFolder + "/assets/scenes/default.tscene");

        // 4. Copy all current asset files alongside the exe
        if (fs::exists("assets")) {
            fs::copy("assets", outputFolder + "/assets",
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }

        // 5. Write game.mode marker so the exe launches without editor UI
        {
            std::string ver, auth, cover;
            m_UIManager.GetExportMetadata(ver, auth, cover);
            if (ver.empty()) ver = "1.0.0";
            if (auth.empty()) auth = m_ProjectName.empty() ? "Unknown" : m_ProjectName;
            if (cover.empty()) cover = "assets/cover.png";
            std::ofstream marker(outputFolder + "/game.mode");
            marker << "# tsuEngine standalone game marker\n";
            marker << "title=" << gameName << "\n";
            marker << "version=" << ver << "\n";
            marker << "author=" << auth << "\n";
            marker << "cover=" << cover << "\n";
        }

        m_UIManager.Log("[Export] Game exported to: " + outputFolder);
        m_UIManager.Log("[Export] Executable: " + gameName + ".exe");
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Export] Error: ") + ex.what());
    }
}

void Application::HandleEditorInput(int winW, int winH)
{
    double mxd, myd;
    InputManager::GetMousePosition(mxd, myd);
    float mx = (float)mxd;
    float my = (float)myd;

    // Track RMB press position (before early-return so we always capture it)
    if (InputManager::IsMouseDown(Mouse::Right) && !m_UIManager.WantCaptureMouse())
    {
        m_RmbPressX = mx;
        m_RmbPressY = my;
    }

    // RMB released with small movement = viewport right-click → context menu
    if (InputManager::IsMouseUp(Mouse::Right) && !m_UIManager.WantCaptureMouse())
    {
        float dx = mx - m_RmbPressX;
        float dy = my - m_RmbPressY;
        if (dx*dx + dy*dy < 25.0f)  // < 5px displacement = click, not drag
        {
            m_RmbHitEntity = RaycastScene(m_RmbPressX, m_RmbPressY, winW, winH);
            m_RmbWorldPos = ComputeViewportSpawnPoint(m_RmbPressX, m_RmbPressY, winW, winH);
            m_OpenViewportMenu = true;
            ImGui::OpenPopup("##vp_ctx");
        }
    }

    // Don't interfere when right mouse is rotating the camera
    if (InputManager::IsMousePressed(Mouse::Right)) return;

    // Don't interfere when ImGui is capturing the mouse (click on panel)
    if (m_UIManager.WantCaptureMouse()) return;

    // Ignore clicks on stacked top UI (menu + toolbar + camera tabs)
    if (my <= (float)UIManager::k_TopStackH) return;

    const MouseDelta delta = InputManager::GetMouseDelta();

    // ---- Mouse held (hold) ----
    if (InputManager::IsMousePressed(Mouse::Left))
    {
        if (m_Gizmo.IsDragging() && m_SelectedEntity >= 0)
        {
            if (m_GizmoMode == GizmoMode::Move)
            {
                glm::vec3 move = m_Gizmo.OnMouseDrag(m_EditorCamera,
                                                       delta.x, delta.y,
                                                       winW, winH);
                m_Scene.Transforms[m_SelectedEntity].Position += move;
            }
            else if (m_GizmoMode == GizmoMode::Rotate)
            {
                float deg = m_Gizmo.OnMouseDragRotation(m_EditorCamera,
                                                         delta.x, delta.y,
                                                         winW, winH);
                auto axis = m_Gizmo.GetDragAxis();
                if (axis == EditorGizmo::Axis::X || axis == EditorGizmo::Axis::YZ)
                    m_Scene.Transforms[m_SelectedEntity].Rotation.x += deg;
                else if (axis == EditorGizmo::Axis::Y || axis == EditorGizmo::Axis::XZ)
                    m_Scene.Transforms[m_SelectedEntity].Rotation.y += deg;
                else if (axis == EditorGizmo::Axis::Z || axis == EditorGizmo::Axis::XY)
                    m_Scene.Transforms[m_SelectedEntity].Rotation.z += deg;
            }
            else if (m_GizmoMode == GizmoMode::Scale)
            {
                glm::vec3 sd = m_Gizmo.OnMouseDragScale(m_EditorCamera,
                                                          delta.x, delta.y,
                                                          winW, winH);
                m_Scene.Transforms[m_SelectedEntity].Scale += sd;
                // Clamp scale to prevent going negative
                auto& s = m_Scene.Transforms[m_SelectedEntity].Scale;
                s.x = std::max(s.x, 0.01f);
                s.y = std::max(s.y, 0.01f);
                s.z = std::max(s.z, 0.01f);
            }
            return;
        }
    }

    // ---- Mouse down (click) ----
    if (InputManager::IsMouseDown(Mouse::Left))
    {
        // Try first to hit a gizmo axis
        if (m_SelectedEntity >= 0)
        {
            int gizmoHitMode = (m_GizmoMode == GizmoMode::Rotate) ? 1 : 0;
            auto axis = m_Gizmo.OnMouseDown(
                m_Scene.GetEntityWorldPos(m_SelectedEntity),
                m_EditorCamera, mx, my, winW, winH, gizmoHitMode);

            if (axis != EditorGizmo::Axis::None)
                return; // started gizmo drag
        }

        // Raycast for entity selection
        m_SelectedEntity = RaycastScene(mx, my, winW, winH);
        return;
    }

    // ---- Mouse released ----
    if (InputManager::IsMouseUp(Mouse::Left))
    {
        m_Gizmo.OnMouseUp();
    }

    // Gizmo mode hotkeys: W = Move, E = Rotate, R = Scale
    if (InputManager::IsKeyDown(Key::W)) m_GizmoMode = GizmoMode::Move;
    if (InputManager::IsKeyDown(Key::E)) m_GizmoMode = GizmoMode::Scale;
    if (InputManager::IsKeyDown(Key::R)) m_GizmoMode = GizmoMode::Rotate;
}

// ----------------------------------------------------------------
// RebuildPrefabPreview — populate m_PrefabPreviewScene from a PrefabAsset
// ----------------------------------------------------------------
void Application::RebuildPrefabPreview()
{
    int idx = m_UIManager.GetEditingPrefabIdx();
    if (idx < 0 || idx >= (int)m_Scene.Prefabs.size()) return;

    const PrefabAsset& prefab = m_Scene.Prefabs[idx];

    // Clear old preview scene completely
    FreeSceneMeshes(m_PrefabPreviewScene);
    m_PrefabPreviewScene = Scene();

    // Copy materials from main scene so mesh materials resolve
    m_PrefabPreviewScene.Materials = m_Scene.Materials;

    // Add a default directional light so the prefab is visible
    {
        Entity lightEnt = m_PrefabPreviewScene.CreateEntity("_PreviewLight");
        int li = (int)lightEnt.GetID();
        m_PrefabPreviewScene.Transforms[li].Position = glm::vec3(5.0f, 8.0f, 5.0f);
        m_PrefabPreviewScene.Transforms[li].Rotation = glm::vec3(-45.0f, 30.0f, 0.0f);
        m_PrefabPreviewScene.Lights[li].Active    = true;
        m_PrefabPreviewScene.Lights[li].Enabled   = true;
        m_PrefabPreviewScene.Lights[li].Type      = LightType::Directional;
        m_PrefabPreviewScene.Lights[li].Intensity = 1.5f;
        m_PrefabPreviewScene.Lights[li].Color     = glm::vec3(1.0f);
    }

    // Create entities from prefab nodes (offset by 1 because of light entity)
    for (int ni = 0; ni < (int)prefab.Nodes.size(); ++ni)
    {
        const PrefabEntityData& node = prefab.Nodes[ni];
        Entity ent = m_PrefabPreviewScene.CreateEntity(node.Name);
        int ei = (int)ent.GetID();

        m_PrefabPreviewScene.Transforms[ei] = node.Transform;

        // Resolve parent (offset by 1 for the preview light entity)
        if (node.ParentIdx >= 0)
            m_PrefabPreviewScene.SetEntityParent(ei, node.ParentIdx + 1);

        // Create mesh
        if (!node.MeshType.empty())
        {
            Mesh* mesh = nullptr;
            const std::string& mt = node.MeshType;
            if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
            else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
            else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
            else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
            else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
            else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
            else if (mt.size() > 4 && mt.substr(0,4) == "obj:")
                mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
            if (mesh) {
                m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr  = mesh;
                m_PrefabPreviewScene.MeshRenderers[ei].MeshType = mt;
            }
        }

        // Resolve material by name
        if (!node.MaterialName.empty())
        {
            for (int mi = 0; mi < (int)m_PrefabPreviewScene.Materials.size(); ++mi)
                if (m_PrefabPreviewScene.Materials[mi].Name == node.MaterialName)
                    { m_PrefabPreviewScene.EntityMaterial[ei] = mi; break; }
        }

        // Light
        if (node.HasLight)
        {
            m_PrefabPreviewScene.Lights[ei] = node.Light;
            m_PrefabPreviewScene.Lights[ei].Active  = true;
            m_PrefabPreviewScene.Lights[ei].Enabled = true;
        }
    }

    m_PrefabPreviewIdx = idx;
}

// ----------------------------------------------------------------
// RebuildRoomPreview — populate m_RoomPreviewScene from room blocks
// ----------------------------------------------------------------
void Application::RebuildRoomPreview()
{
    int rsi = m_UIManager.GetEditingRoomSetIdx();
    int rri = m_UIManager.GetEditingRoomIdx();
    if (rsi < 0 || rsi >= (int)m_Scene.RoomSets.size()) return;
    if (rri < 0 || rri >= (int)m_Scene.RoomSets[rsi].Rooms.size()) return;

    const RoomTemplate& room = m_Scene.RoomSets[rsi].Rooms[rri];

    // Clear preview scene
    FreeSceneMeshes(m_RoomPreviewScene);
    m_RoomPreviewScene = Scene();
    m_RoomPreviewScene.Materials = m_Scene.Materials;

    // Preview light
    {
        Entity lightEnt = m_RoomPreviewScene.CreateEntity("_PreviewLight");
        int li = (int)lightEnt.GetID();
        m_RoomPreviewScene.Transforms[li].Position = glm::vec3(5.0f, 12.0f, 5.0f);
        m_RoomPreviewScene.Transforms[li].Rotation = glm::vec3(-50.0f, 30.0f, 0.0f);
        m_RoomPreviewScene.Lights[li].Active    = true;
        m_RoomPreviewScene.Lights[li].Enabled   = true;
        m_RoomPreviewScene.Lights[li].Type      = LightType::Directional;
        m_RoomPreviewScene.Lights[li].Intensity = 1.5f;
        m_RoomPreviewScene.Lights[li].Color     = glm::vec3(1.0f);
    }

    // Interior prefab nodes (offset by 1: preview light only)
    for (int ni = 0; ni < (int)room.Interior.size(); ++ni)
    {
        const PrefabEntityData& node = room.Interior[ni];
        Entity ent = m_RoomPreviewScene.CreateEntity(node.Name);
        int ei = (int)ent.GetID();
        m_RoomPreviewScene.Transforms[ei] = node.Transform;

        if (!node.MeshType.empty())
        {
            Mesh* mesh = nullptr;
            const std::string& mt = node.MeshType;
            if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
            else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
            else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
            else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
            else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
            else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
            else if (mt.size() > 4 && mt.substr(0,4) == "obj:")
                mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
            if (mesh) {
                m_RoomPreviewScene.MeshRenderers[ei].MeshPtr  = mesh;
                m_RoomPreviewScene.MeshRenderers[ei].MeshType = mt;
            }
        }

        if (!node.MaterialName.empty())
        {
            for (int mi = 0; mi < (int)m_RoomPreviewScene.Materials.size(); ++mi)
                if (m_RoomPreviewScene.Materials[mi].Name == node.MaterialName)
                    { m_RoomPreviewScene.EntityMaterial[ei] = mi; break; }
        }

        // Light
        if (node.HasLight)
        {
            m_RoomPreviewScene.Lights[ei] = node.Light;
            m_RoomPreviewScene.Lights[ei].Active  = true;
            m_RoomPreviewScene.Lights[ei].Enabled = true;
        }

        // Camera
        if (node.HasCamera)
        {
            m_RoomPreviewScene.GameCameras[ei] = node.Camera;
            m_RoomPreviewScene.GameCameras[ei].Active = true;
        }

        // Parent hierarchy
        if (node.ParentIdx >= 0)
            m_RoomPreviewScene.SetEntityParent(ei, node.ParentIdx + 1);
    }

    // ---- Add solid cube entities for room blocks (floor / walls) ----
    for (int bi = 0; bi < (int)room.Blocks.size(); ++bi)
    {
        const auto& blk = room.Blocks[bi];
        Entity ent = m_RoomPreviewScene.CreateEntity("_Block" + std::to_string(bi));
        int ei = (int)ent.GetID();
        m_RoomPreviewScene.Transforms[ei].Position = glm::vec3((float)blk.X, (float)blk.Y, (float)blk.Z);
        m_RoomPreviewScene.Transforms[ei].Scale    = glm::vec3(1.0f);
        Mesh* mesh = new Mesh(Mesh::CreateCube("#888888"));
        m_RoomPreviewScene.MeshRenderers[ei].MeshPtr  = mesh;
        m_RoomPreviewScene.MeshRenderers[ei].MeshType = "cube";
    }

    // ---- Apply lightmap to every preview entity (if room was baked) ----
    if (!room.LightmapPath.empty())
    {
        // Load once; subsequent frames use cached ID without incrementing refcount
        if (m_RoomPreviewLightmap != room.LightmapPath)
        {
            if (!m_RoomPreviewLightmap.empty())
                LightmapManager::Instance().Release(m_RoomPreviewLightmap);
            LightmapManager::Instance().Load(room.LightmapPath);
            m_RoomPreviewLightmap = room.LightmapPath;
        }
        unsigned int lmID = LightmapManager::Instance().GetCachedID(room.LightmapPath);
        if (lmID != 0)
        {
            for (int ei = 1; ei < (int)m_RoomPreviewScene.MeshRenderers.size(); ++ei)
            {
                m_RoomPreviewScene.MeshRenderers[ei].LightmapID = lmID;
                m_RoomPreviewScene.MeshRenderers[ei].LightmapST = room.LightmapST;
            }
        }
    }
    else if (!m_RoomPreviewLightmap.empty())
    {
        LightmapManager::Instance().Release(m_RoomPreviewLightmap);
        m_RoomPreviewLightmap.clear();
    }

    m_RoomPreviewSetIdx  = rsi;
    m_RoomPreviewRoomIdx = rri;
    m_RoomPreviewBlockCount = (int)room.Blocks.size();
    m_RoomPreviewInteriorCount = (int)room.Interior.size();
}

// ----------------------------------------------------------------
// SyncPrefabPreviewBack — write preview entity transforms back to prefab nodes
// ----------------------------------------------------------------
void Application::SyncPrefabPreviewBack()
{
    int idx = m_PrefabPreviewIdx;
    if (idx < 0 || idx >= (int)m_Scene.Prefabs.size()) return;

    PrefabAsset& prefab = m_Scene.Prefabs[idx];
    // Entity 0 = preview light, entities 1..N = prefab nodes 0..N-1
    for (int ni = 0; ni < (int)prefab.Nodes.size(); ++ni)
    {
        int ei = ni + 1;
        if (ei >= (int)m_PrefabPreviewScene.Transforms.size()) break;
        prefab.Nodes[ni].Transform = m_PrefabPreviewScene.Transforms[ei];
        prefab.Nodes[ni].Name      = m_PrefabPreviewScene.EntityNames[ei];
    }
}

// ----------------------------------------------------------------
// HandlePrefabEditorInput — gizmo for prefab preview entities
// ----------------------------------------------------------------
void Application::HandlePrefabEditorInput(int winW, int winH)
{
    if (!m_UIManager.IsPrefabEditorActive()) return;
    if (m_PrefabPreviewIdx < 0) return;

    double mxd, myd;
    InputManager::GetMousePosition(mxd, myd);
    float mx = (float)mxd;
    float my = (float)myd;

    if (InputManager::IsMousePressed(Mouse::Right)) return;
    if (m_UIManager.WantCaptureMouse()) return;
    if (my <= (float)UIManager::k_TopStackH) return;

    int selNode = m_UIManager.GetPrefabSelectedNode();
    // Map node index to preview entity index (offset by 1 for light)
    int selEntity = (selNode >= 0) ? selNode + 1 : -1;
    if (selEntity >= (int)m_PrefabPreviewScene.Transforms.size()) selEntity = -1;

    const MouseDelta delta = InputManager::GetMouseDelta();

    // ---- Mouse held (drag) ----
    if (InputManager::IsMousePressed(Mouse::Left))
    {
        if (m_Gizmo.IsDragging() && selEntity >= 0)
        {
            if (m_GizmoMode == GizmoMode::Move)
            {
                glm::vec3 move = m_Gizmo.OnMouseDrag(m_EditorCamera,
                                                       delta.x, delta.y,
                                                       winW, winH);
                m_PrefabPreviewScene.Transforms[selEntity].Position += move;
            }
            else if (m_GizmoMode == GizmoMode::Rotate)
            {
                float deg = m_Gizmo.OnMouseDragRotation(m_EditorCamera,
                                                         delta.x, delta.y,
                                                         winW, winH);
                auto axis = m_Gizmo.GetDragAxis();
                if (axis == EditorGizmo::Axis::X || axis == EditorGizmo::Axis::YZ)
                    m_PrefabPreviewScene.Transforms[selEntity].Rotation.x += deg;
                else if (axis == EditorGizmo::Axis::Y || axis == EditorGizmo::Axis::XZ)
                    m_PrefabPreviewScene.Transforms[selEntity].Rotation.y += deg;
                else if (axis == EditorGizmo::Axis::Z || axis == EditorGizmo::Axis::XY)
                    m_PrefabPreviewScene.Transforms[selEntity].Rotation.z += deg;
            }
            else if (m_GizmoMode == GizmoMode::Scale)
            {
                glm::vec3 sd = m_Gizmo.OnMouseDragScale(m_EditorCamera,
                                                          delta.x, delta.y,
                                                          winW, winH);
                m_PrefabPreviewScene.Transforms[selEntity].Scale += sd;
                auto& s = m_PrefabPreviewScene.Transforms[selEntity].Scale;
                s.x = std::max(s.x, 0.01f);
                s.y = std::max(s.y, 0.01f);
                s.z = std::max(s.z, 0.01f);
            }
            // Sync this specific node back to the prefab immediately
            SyncPrefabPreviewBack();
            return;
        }
    }

    // ---- Mouse down (click) ----
    if (InputManager::IsMouseDown(Mouse::Left))
    {
        // Try gizmo hit first
        if (selEntity >= 0)
        {
            int gizmoHitMode = (m_GizmoMode == GizmoMode::Rotate) ? 1 : 0;
            auto axis = m_Gizmo.OnMouseDown(
                m_PrefabPreviewScene.Transforms[selEntity].Position,
                m_EditorCamera, mx, my, winW, winH, gizmoHitMode);
            if (axis != EditorGizmo::Axis::None)
                return;
        }

        // Raycast the preview scene for node selection
        float aspect = (float)winW / (float)winH;
        glm::mat4 vp = m_EditorCamera.GetProjection(aspect) * m_EditorCamera.GetViewMatrix();
        glm::mat4 vpI = glm::inverse(vp);
        float ndcX =  2.0f * mx / (float)winW - 1.0f;
        float ndcY = -2.0f * my / (float)winH + 1.0f;
        glm::vec4 nearH = vpI * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 farH  = vpI * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
        glm::vec3 orig = glm::vec3(nearH) / nearH.w;
        glm::vec3 dir  = glm::normalize(glm::vec3(farH) / farH.w - orig);

        float bestT = 1e30f;
        int hitNode = -1;
        // Skip entity 0 (preview light)
        for (int ei = 1; ei < (int)m_PrefabPreviewScene.Transforms.size(); ++ei)
        {
            bool hasMesh  = m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr != nullptr;
            bool hasLight = (ei < (int)m_PrefabPreviewScene.Lights.size() && m_PrefabPreviewScene.Lights[ei].Active);
            bool hasCam   = (ei < (int)m_PrefabPreviewScene.GameCameras.size() && m_PrefabPreviewScene.GameCameras[ei].Active);
            if (!hasMesh && !hasLight && !hasCam) continue;
            glm::vec3 pos  = m_PrefabPreviewScene.GetEntityWorldPos(ei);
            glm::vec3 sc   = m_PrefabPreviewScene.Transforms[ei].Scale;
            float radius   = hasMesh ? std::max({sc.x, sc.y, sc.z}) * 0.5f : 0.35f;
            glm::vec3 oc   = orig - pos;
            float b = glm::dot(oc, dir);
            float c = glm::dot(oc, oc) - radius * radius;
            float disc = b*b - c;
            if (disc < 0.0f) continue;
            float t = -b - sqrtf(disc);
            if (t < 0.0f) t = -b + sqrtf(disc);
            if (t > 0.0f && t < bestT) { bestT = t; hitNode = ei - 1; }
        }
        m_UIManager.SetPrefabSelectedNode(hitNode);
        return;
    }

    if (InputManager::IsMouseUp(Mouse::Left))
        m_Gizmo.OnMouseUp();

    if (InputManager::IsKeyDown(Key::W)) m_GizmoMode = GizmoMode::Move;
    if (InputManager::IsKeyDown(Key::E)) m_GizmoMode = GizmoMode::Rotate;
    if (InputManager::IsKeyDown(Key::R)) m_GizmoMode = GizmoMode::Scale;
}

// ----------------------------------------------------------------
// SyncRoomPreviewBack — write preview entity transforms back to room Interior
// ----------------------------------------------------------------
void Application::SyncRoomPreviewBack()
{
    int rsi = m_RoomPreviewSetIdx;
    int rri = m_RoomPreviewRoomIdx;
    if (rsi < 0 || rsi >= (int)m_Scene.RoomSets.size()) return;
    if (rri < 0 || rri >= (int)m_Scene.RoomSets[rsi].Rooms.size()) return;

    RoomTemplate& room = m_Scene.RoomSets[rsi].Rooms[rri];
    // Entity 0 = preview light, entities 1..N = Interior nodes 0..N-1
    for (int ni = 0; ni < (int)room.Interior.size(); ++ni)
    {
        int ei = ni + 1;
        if (ei >= (int)m_RoomPreviewScene.Transforms.size()) break;
        room.Interior[ni].Transform = m_RoomPreviewScene.Transforms[ei];
        room.Interior[ni].Name      = m_RoomPreviewScene.EntityNames[ei];

        // Sync mesh type
        if (ei < (int)m_RoomPreviewScene.MeshRenderers.size())
            room.Interior[ni].MeshType = m_RoomPreviewScene.MeshRenderers[ei].MeshType;

        // Sync material name
        int matIdx = (ei < (int)m_RoomPreviewScene.EntityMaterial.size())
                     ? m_RoomPreviewScene.EntityMaterial[ei] : -1;
        if (matIdx >= 0 && matIdx < (int)m_RoomPreviewScene.Materials.size())
            room.Interior[ni].MaterialName = m_RoomPreviewScene.Materials[matIdx].Name;
        else
            room.Interior[ni].MaterialName.clear();

        // Sync light
        if (ei < (int)m_RoomPreviewScene.Lights.size() && m_RoomPreviewScene.Lights[ei].Active)
        {
            room.Interior[ni].HasLight = true;
            room.Interior[ni].Light    = m_RoomPreviewScene.Lights[ei];
        }
        else
        {
            room.Interior[ni].HasLight = false;
        }

        // Sync camera
        if (ei < (int)m_RoomPreviewScene.GameCameras.size() && m_RoomPreviewScene.GameCameras[ei].Active)
        {
            room.Interior[ni].HasCamera = true;
            room.Interior[ni].Camera    = m_RoomPreviewScene.GameCameras[ei];
        }
        else
        {
            room.Interior[ni].HasCamera = false;
        }
    }
}

// ----------------------------------------------------------------
// Room editor Undo/Redo helpers
// ----------------------------------------------------------------
RoomTemplate* Application::GetEditingRoom()
{
    int rsi = m_UIManager.GetEditingRoomSetIdx();
    int rri = m_UIManager.GetEditingRoomIdx();
    if (rsi < 0 || rsi >= (int)m_Scene.RoomSets.size()) return nullptr;
    if (rri < 0 || rri >= (int)m_Scene.RoomSets[rsi].Rooms.size()) return nullptr;
    return &m_Scene.RoomSets[rsi].Rooms[rri];
}

void Application::PushRoomEditState()
{
    RoomTemplate* room = GetEditingRoom();
    if (!room) return;
    RoomEditState state;
    state.Blocks = room->Blocks;
    state.Doors  = room->Doors;
    if ((int)m_RoomUndoStack.size() >= k_MaxRoomHistory)
        m_RoomUndoStack.erase(m_RoomUndoStack.begin());
    m_RoomUndoStack.push_back(std::move(state));
    m_RoomRedoStack.clear();
}

void Application::DoRoomUndo()
{
    if (m_RoomUndoStack.empty()) return;
    RoomTemplate* room = GetEditingRoom();
    if (!room) return;
    // Push current state to redo
    RoomEditState cur;
    cur.Blocks = room->Blocks;
    cur.Doors  = room->Doors;
    m_RoomRedoStack.push_back(std::move(cur));
    // Restore last undo state
    RoomEditState& prev = m_RoomUndoStack.back();
    room->Blocks = std::move(prev.Blocks);
    room->Doors  = std::move(prev.Doors);
    m_RoomUndoStack.pop_back();
}

void Application::DoRoomRedo()
{
    if (m_RoomRedoStack.empty()) return;
    RoomTemplate* room = GetEditingRoom();
    if (!room) return;
    // Push current state to undo
    RoomEditState cur;
    cur.Blocks = room->Blocks;
    cur.Doors  = room->Doors;
    m_RoomUndoStack.push_back(std::move(cur));
    // Restore redo state
    RoomEditState& next = m_RoomRedoStack.back();
    room->Blocks = std::move(next.Blocks);
    room->Doors  = std::move(next.Doors);
    m_RoomRedoStack.pop_back();
}

// ----------------------------------------------------------------
// HandleRoomEditorInput — gizmo + selection for room preview entities
//                         + expansion arrow hover/click detection
// ----------------------------------------------------------------
void Application::HandleRoomEditorInput(int winW, int winH)
{
    if (!m_UIManager.IsMazeRoomEditorOpen()) return;
    if (m_RoomPreviewSetIdx < 0) return;

    double mxd, myd;
    InputManager::GetMousePosition(mxd, myd);
    float mx = (float)mxd;
    float my = (float)myd;

    if (InputManager::IsMousePressed(Mouse::Right)) return;
    if (m_UIManager.WantCaptureMouse()) return;
    if (my <= (float)UIManager::k_TopStackH) return;

    int selNode = m_UIManager.GetRoomSelectedNode();
    int selEntity = (selNode >= 0) ? selNode + 1 : -1;
    if (selEntity >= (int)m_RoomPreviewScene.Transforms.size()) selEntity = -1;

    const MouseDelta delta = InputManager::GetMouseDelta();

    // ---- Mouse held (drag gizmo) ----
    if (InputManager::IsMousePressed(Mouse::Left))
    {
        if (m_Gizmo.IsDragging() && selEntity >= 0)
        {
            if (m_GizmoMode == GizmoMode::Move)
            {
                glm::vec3 move = m_Gizmo.OnMouseDrag(m_RoomEditorCamera,
                                                       delta.x, delta.y,
                                                       winW, winH);
                m_RoomPreviewScene.Transforms[selEntity].Position += move;
            }
            else if (m_GizmoMode == GizmoMode::Rotate)
            {
                float deg = m_Gizmo.OnMouseDragRotation(m_RoomEditorCamera,
                                                         delta.x, delta.y,
                                                         winW, winH);
                auto axis = m_Gizmo.GetDragAxis();
                if (axis == EditorGizmo::Axis::X || axis == EditorGizmo::Axis::YZ)
                    m_RoomPreviewScene.Transforms[selEntity].Rotation.x += deg;
                else if (axis == EditorGizmo::Axis::Y || axis == EditorGizmo::Axis::XZ)
                    m_RoomPreviewScene.Transforms[selEntity].Rotation.y += deg;
                else if (axis == EditorGizmo::Axis::Z || axis == EditorGizmo::Axis::XY)
                    m_RoomPreviewScene.Transforms[selEntity].Rotation.z += deg;
            }
            else if (m_GizmoMode == GizmoMode::Scale)
            {
                glm::vec3 sd = m_Gizmo.OnMouseDragScale(m_RoomEditorCamera,
                                                          delta.x, delta.y,
                                                          winW, winH);
                m_RoomPreviewScene.Transforms[selEntity].Scale += sd;
                auto& s = m_RoomPreviewScene.Transforms[selEntity].Scale;
                s.x = std::max(s.x, 0.01f);
                s.y = std::max(s.y, 0.01f);
                s.z = std::max(s.z, 0.01f);
            }
            SyncRoomPreviewBack();
            return;
        }
    }

    // ---- Build ray from camera every frame (used for arrow + block hover) ----
    float aspect = (float)winW / (float)winH;
    glm::mat4 vp  = m_RoomEditorCamera.GetProjection(aspect) * m_RoomEditorCamera.GetViewMatrix();
    glm::mat4 vpI = glm::inverse(vp);
    float ndcX =  2.0f * mx / (float)winW - 1.0f;
    float ndcY = -2.0f * my / (float)winH + 1.0f;
    glm::vec4 nearH = vpI * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = vpI * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    glm::vec3 rayOrig = glm::vec3(nearH) / nearH.w;
    glm::vec3 rayDir  = glm::normalize(glm::vec3(farH) / farH.w - rayOrig);

    // ---- Compute per-block exposed face arrows (only when blocks are visible) ----
    m_MazeBlockArrows.clear();
    m_MazeHoveredArrowIdx = -1;
    m_MazeHoveredBlock    = -1;
    if (m_UIManager.GetRoomShowBlocks())
    {
        int rsi = m_UIManager.GetEditingRoomSetIdx();
        int rri = m_UIManager.GetEditingRoomIdx();
        if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
            rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
        {
            const RoomTemplate& room = m_Scene.RoomSets[rsi].Rooms[rri];
            const int dx[6] = {1,-1, 0, 0, 0, 0};
            const int dy[6] = {0, 0, 0, 0, 1,-1};
            const int dz[6] = {0, 0, 1,-1, 0, 0};

            auto hasBlock = [&](int nx, int ny, int nz) {
                for (const auto& b : room.Blocks)
                    if (b.X == nx && b.Y == ny && b.Z == nz) return true;
                return false;
            };

            // One arrow per exposed block face
            for (int bi = 0; bi < (int)room.Blocks.size(); ++bi)
            {
                const auto& blk = room.Blocks[bi];
                for (int d = 0; d < 6; ++d)
                {
                    if (!hasBlock(blk.X+dx[d], blk.Y+dy[d], blk.Z+dz[d]))
                    {
                        BlockFaceArrow fa;
                        fa.pos = glm::vec3(blk.X + dx[d]*0.6f,
                                           blk.Y + dy[d]*0.6f,
                                           blk.Z + dz[d]*0.6f);
                        fa.dir      = d;
                        fa.blockIdx = bi;
                        m_MazeBlockArrows.push_back(fa);
                    }
                }
            }

            // Sphere-ray intersection for hovered arrow
            float bestT = 1e30f;
            constexpr float kR = 0.25f;
            for (int ai = 0; ai < (int)m_MazeBlockArrows.size(); ++ai)
            {
                glm::vec3 oc = rayOrig - m_MazeBlockArrows[ai].pos;
                float b2 = glm::dot(oc, rayDir);
                float c2 = glm::dot(oc, oc) - kR * kR;
                float disc = b2*b2 - c2;
                if (disc < 0.0f) continue;
                float t = -b2 - sqrtf(disc);
                if (t < 0.0f) t = -b2 + sqrtf(disc);
                if (t > 0.0f && t < bestT) { bestT = t; m_MazeHoveredArrowIdx = ai; }
            }

            // Y-plane raycast for block hover highlight
            int layer = m_UIManager.GetRoomEditorLayer();
            float ly  = (float)layer;
            if (std::abs(rayDir.y) > 0.0001f)
            {
                float t = (ly - rayOrig.y) / rayDir.y;
                if (t > 0.0f)
                {
                    glm::vec3 hit = rayOrig + rayDir * t;
                    int gx = (int)std::round(hit.x);
                    int gz = (int)std::round(hit.z);
                    for (int bi = 0; bi < (int)room.Blocks.size(); ++bi)
                        if (room.Blocks[bi].X == gx && room.Blocks[bi].Z == gz && room.Blocks[bi].Y == layer)
                        { m_MazeHoveredBlock = bi; break; }
                }
            }
        }
    }

    // ---- Mouse down (click) ----
    if (InputManager::IsMouseDown(Mouse::Left))
    {
        // === Arrow click: add block (normal) or erase block (shift) ===
        if (m_MazeHoveredArrowIdx >= 0)
        {
            const BlockFaceArrow& fa = m_MazeBlockArrows[m_MazeHoveredArrowIdx];
            int rsi = m_UIManager.GetEditingRoomSetIdx();
            int rri = m_UIManager.GetEditingRoomIdx();
            bool shiftHeld = InputManager::IsKeyPressed(Key::LeftShift);
            if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
            {
                RoomTemplate& rm = m_Scene.RoomSets[rsi].Rooms[rri];
                const int dx[6] = {1,-1, 0, 0, 0, 0};
                const int dy[6] = {0, 0, 0, 0, 1,-1};
                const int dz[6] = {0, 0, 1,-1, 0, 0};
                // Capture block coords before any erasure invalidates references
                int bx = rm.Blocks[fa.blockIdx].X;
                int by = rm.Blocks[fa.blockIdx].Y;
                int bz = rm.Blocks[fa.blockIdx].Z;

                if (m_UIManager.IsDoorMode())
                {
                    // Door mode: toggle door on this exposed face (horizontal faces only)
                    // dir: 0=+X 1=-X 2=+Z 3=-Z  (skip 4=+Y 5=-Y — no doors on floor/ceiling)
                    if (fa.dir <= 3)
                    {
                        DoorFace df = (DoorFace)fa.dir;
                        PushRoomEditState();
                        bool found = false;
                        for (int di = (int)rm.Doors.size() - 1; di >= 0; --di)
                        {
                            auto& d = rm.Doors[di];
                            if (d.BlockX == bx && d.BlockZ == bz && d.BlockY == by && d.Face == df)
                            {
                                rm.Doors.erase(rm.Doors.begin() + di);
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            DoorPlacement dp;
                            dp.BlockX = bx; dp.BlockZ = bz; dp.BlockY = by; dp.Face = df;
                            rm.Doors.push_back(dp);
                        }
                    }
                }
                else if (shiftHeld)
                {
                    // Erase: remove the block that owns this arrow (keep at least 1)
                    if ((int)rm.Blocks.size() > 1)
                    {
                        PushRoomEditState();
                        rm.Blocks.erase(rm.Blocks.begin() + fa.blockIdx);
                        rm.Doors.erase(std::remove_if(rm.Doors.begin(), rm.Doors.end(),
                            [bx,by,bz](const DoorPlacement& d) {
                                return d.BlockX==bx && d.BlockY==by && d.BlockZ==bz;
                            }), rm.Doors.end());
                    }
                }
                else
                {
                    // Add: place new block adjacent to this face
                    int nx = bx + dx[fa.dir];
                    int ny = by + dy[fa.dir];
                    int nz = bz + dz[fa.dir];
                    bool exists = false;
                    for (const auto& b : rm.Blocks)
                        if (b.X==nx && b.Y==ny && b.Z==nz) { exists=true; break; }
                    if (!exists) {
                        PushRoomEditState();
                        RoomBlock nb; nb.X=nx; nb.Y=ny; nb.Z=nz; rm.Blocks.push_back(nb);
                    }
                }
            }
            m_UIManager.BlockNextRoomClick();
            m_UIManager.CancelPendingRoomClick();
            return;
        }

        // === Gizmo hit on interior node ===
        if (selEntity >= 0)
        {
            int gizmoHitMode = (m_GizmoMode == GizmoMode::Rotate) ? 1 : 0;
            auto axis = m_Gizmo.OnMouseDown(
                m_RoomPreviewScene.Transforms[selEntity].Position,
                m_RoomEditorCamera, mx, my, winW, winH, gizmoHitMode);
            if (axis != EditorGizmo::Axis::None)
                return;
        }

        // === Raycast preview scene for interior node selection ===
        float bestT = 1e30f;
        int hitNode = -1;
        for (int ei = 1; ei < (int)m_RoomPreviewScene.Transforms.size(); ++ei)
        {
            bool hasMesh  = m_RoomPreviewScene.MeshRenderers[ei].MeshPtr != nullptr;
            bool hasLight = (ei < (int)m_RoomPreviewScene.Lights.size() && m_RoomPreviewScene.Lights[ei].Active);
            bool hasCam   = (ei < (int)m_RoomPreviewScene.GameCameras.size() && m_RoomPreviewScene.GameCameras[ei].Active);
            if (!hasMesh && !hasLight && !hasCam) continue;
            glm::vec3 pos  = m_RoomPreviewScene.GetEntityWorldPos(ei);
            glm::vec3 sc   = m_RoomPreviewScene.Transforms[ei].Scale;
            float radius   = hasMesh ? std::max({sc.x, sc.y, sc.z}) * 0.5f : 0.35f;
            glm::vec3 oc   = rayOrig - pos;
            float b2 = glm::dot(oc, rayDir);
            float c2 = glm::dot(oc, oc) - radius * radius;
            float disc = b2 * b2 - c2;
            if (disc < 0.0f) continue;
            float t = -b2 - sqrtf(disc);
            if (t < 0.0f) t = -b2 + sqrtf(disc);
            if (t > 0.0f && t < bestT) { bestT = t; hitNode = ei - 1; }
        }
        m_UIManager.SetRoomSelectedNode(hitNode);
        return;
    }

    if (InputManager::IsMouseUp(Mouse::Left))
        m_Gizmo.OnMouseUp();

    if (InputManager::IsKeyDown(Key::W)) m_GizmoMode = GizmoMode::Move;
    if (InputManager::IsKeyDown(Key::E)) m_GizmoMode = GizmoMode::Rotate;
    if (InputManager::IsKeyDown(Key::R)) m_GizmoMode = GizmoMode::Scale;

    // ---- Undo / Redo (Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z) ----
    bool ctrlDown = InputManager::IsKeyPressed(Key::LeftControl) ||
                    InputManager::IsKeyPressed(Key::RightControl);
    bool shiftDown = InputManager::IsKeyPressed(Key::LeftShift) ||
                     InputManager::IsKeyPressed(Key::RightShift);
    if (ctrlDown && InputManager::IsKeyDown(Key::Z))
    {
        if (shiftDown) DoRoomRedo();
        else           DoRoomUndo();
    }
    if (ctrlDown && InputManager::IsKeyDown(Key::Y)) DoRoomRedo();
}

glm::vec3 Application::ComputeViewportSpawnPoint(float mx, float my, int winW, int winH) const
{
    float aspect  = (float)winW / (float)winH;
    glm::mat4 vp  = m_EditorCamera.GetProjection(aspect) * m_EditorCamera.GetViewMatrix();
    glm::mat4 vpI = glm::inverse(vp);

    float ndcX =  2.0f * mx / (float)winW - 1.0f;
    float ndcY = -2.0f * my / (float)winH + 1.0f;

    glm::vec4 nearH = vpI * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = vpI * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearH /= nearH.w;
    farH  /= farH.w;

    glm::vec3 orig = glm::vec3(nearH);
    glm::vec3 dir  = glm::normalize(glm::vec3(farH - nearH));

    // Intersect with Y=0 plane; fallback to fixed distance in view direction.
    if (std::abs(dir.y) > 1e-4f)
    {
        float t = -orig.y / dir.y;
        if (t > 0.0f)
            return orig + dir * t;
    }
    return orig + dir * 5.0f;
}

void Application::CreateViewportEntity(const std::string& type, int parentEntity, const glm::vec3& worldPos)
{
    const bool isCamera = (type == "Camera");
    std::string baseName = isCamera ? "Camera" : type;
    std::string name = MakeUniqueEntityName(m_Scene, baseName);

    Entity e = m_Scene.CreateEntity(name);
    int id   = (int)e.GetID();

    if (!isCamera)
    {
        std::string color = "#DDDDDD";
        Mesh* mesh = nullptr;
        if      (type == "Cube")     mesh = new Mesh(Mesh::CreateCube(color));
        else if (type == "Sphere")   mesh = new Mesh(Mesh::CreateSphere(color));
        else if (type == "Pyramid")  mesh = new Mesh(Mesh::CreatePyramid(color));
        else if (type == "Cylinder") mesh = new Mesh(Mesh::CreateCylinder(color));
        else if (type == "Capsule")  mesh = new Mesh(Mesh::CreateCapsule(color));
        else if (type == "Plane")    mesh = new Mesh(Mesh::CreatePlane(color));
        m_Scene.MeshRenderers[id].MeshPtr = mesh;
    }
    else
    {
        m_Scene.GameCameras[id].Active = true;
    }

    if (parentEntity >= 0 && parentEntity < (int)m_Scene.Transforms.size())
    {
        // Spawn at parent world position; after parenting local becomes (0,0,0).
        glm::vec3 parentWorld = m_Scene.GetEntityWorldPos(parentEntity);
        m_Scene.Transforms[id].Position = parentWorld;
        m_Scene.SetEntityParent(id, parentEntity);
    }
    else
    {
        m_Scene.Transforms[id].Position = worldPos;
    }

    m_SelectedEntity = id;
}

void Application::CreateViewportLight(LightType type, int parentEntity, const glm::vec3& worldPos)
{
    static const char* typeNames[] = {
        "Directional Light", "Point Light", "Spot Light", "Area Light"
    };
    std::string name = MakeUniqueEntityName(m_Scene, typeNames[(int)type]);
    Entity e = m_Scene.CreateEntity(name);
    int    id = (int)e.GetID();

    m_Scene.Lights[id].Active  = true;
    m_Scene.Lights[id].Enabled = true;
    m_Scene.Lights[id].Type    = type;
    switch (type)
    {
    case LightType::Point:
        m_Scene.Lights[id].Intensity = 1.5f; m_Scene.Lights[id].Range = 10.0f; break;
    case LightType::Spot:
        m_Scene.Lights[id].Intensity = 2.0f; m_Scene.Lights[id].Range = 15.0f;
        m_Scene.Lights[id].InnerAngle = 25.0f; m_Scene.Lights[id].OuterAngle = 40.0f; break;
    case LightType::Area:
        m_Scene.Lights[id].Intensity = 2.0f; m_Scene.Lights[id].Range = 10.0f;
        m_Scene.Lights[id].Width = 2.0f; m_Scene.Lights[id].Height = 1.0f; break;
    default: m_Scene.Lights[id].Intensity = 1.0f; break;
    }

    if (parentEntity >= 0 && parentEntity < (int)m_Scene.Transforms.size())
    {
        glm::vec3 parentWorld = m_Scene.GetEntityWorldPos(parentEntity);
        m_Scene.Transforms[id].Position = parentWorld;
        m_Scene.SetEntityParent(id, parentEntity);
    }
    else
    {
        m_Scene.Transforms[id].Position = worldPos;
    }
    m_SelectedEntity = id;
}

void Application::UpdatePlayerControllers(float dt)
{
    for (int i = 0; i < (int)m_Scene.PlayerControllers.size(); ++i)
    {
        auto& pc = m_Scene.PlayerControllers[i];
        if (!pc.Active) continue;

        bool inForward = false;
        bool inBack    = false;
        bool inLeft    = false;
        bool inRight   = false;
        bool inRun     = false;
        bool inCrouch  = false;

        if (pc.InputMode == PlayerInputMode::Local)
        {
            inForward = InputManager::IsKeyPressed(pc.KeyForward);
            inBack    = InputManager::IsKeyPressed(pc.KeyBack);
            inLeft    = InputManager::IsKeyPressed(pc.KeyLeft);
            inRight   = InputManager::IsKeyPressed(pc.KeyRight);
            inRun     = InputManager::IsKeyPressed(pc.KeyRun);
            inCrouch  = InputManager::IsKeyPressed(pc.KeyCrouch);
        }
        else
        {
            // Each channel stores a key name string ("W", "Left Shift", etc.)
            auto chKey = [&](int chIdx) -> bool {
                std::string keyName = m_Scene.GetChannelString(chIdx);
                int code = InputManager::KeyNameToCode(keyName.c_str());
                return code > 0 && InputManager::IsKeyPressed(code);
            };
            inForward = chKey(pc.ChForward);
            inBack    = chKey(pc.ChBack);
            inLeft    = chKey(pc.ChLeft);
            inRight   = chKey(pc.ChRight);
            inRun     = chKey(pc.ChRun);
            inCrouch  = chKey(pc.ChCrouch);
        }

        glm::vec3 axis(0.0f);
        // Engine forward = +X, right = +Z (right-hand Y-up: Right = Forward x Up = X x Y = +Z)
        if (inForward) axis.x += 1.0f;
        if (inBack)    axis.x -= 1.0f;
        if (inLeft)    axis.z -= 1.0f;
        if (inRight)   axis.z += 1.0f;
        if (glm::length(axis) > 0.0001f)
            axis = glm::normalize(axis);

        bool running   = inRun    && pc.AllowRun;
        bool crouching = inCrouch && pc.AllowCrouch;

        float speed = pc.WalkSpeed;
        if (running)   speed *= pc.RunMultiplier;
        if (crouching) speed *= pc.CrouchMultiplier;

        // --- Determine movement style from MovementMode ---
        // Modes 1 & 3 = world-space axes; Modes 2 & 4 = entity-local axes.
        // Modes 3 & 4 = noclip (bypass physics collision; reset physics velocity).
        const bool isLocalSpace = (pc.MovementMode == PlayerMovementMode::LocalWithCollision ||
                                   pc.MovementMode == PlayerMovementMode::LocalNoCollision);
        const bool noclip       = (pc.MovementMode == PlayerMovementMode::WorldNoCollision ||
                                   pc.MovementMode == PlayerMovementMode::LocalNoCollision);

        glm::vec3 worldMove;
        if (isLocalSpace)
        {
            // MODE 2 / 4 — Local space: axes are relative to the entity's facing direction.
            // Get world-space forward vector (local -Z projected onto the XZ plane).
            glm::mat4 worldMat = m_Scene.GetEntityWorldMatrix(i);
            glm::vec3 fwd = glm::vec3(worldMat * glm::vec4(0,0,-1,0));
            fwd.y = 0.0f;
            if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
            // Right vector is cross(forward, world-up) = left-perpendicular in XZ plane.
            glm::vec3 right = glm::cross(fwd, glm::vec3(0,1,0));
            worldMove = (fwd * axis.x + right * axis.z) * speed * dt;
        }
        else
        {
            // MODE 1 / 3 — World space: forward = world -Z, right = world +X.
            // Player rotation has NO effect on the movement direction.
            worldMove = glm::vec3(axis.z, 0.0f, -axis.x) * speed * dt;
        }

        m_Scene.Transforms[i].Position += worldMove;

        if (noclip)
        {
            // Noclip: wipe physics velocity so accumulated momentum doesn't fight us.
            m_Scene.RigidBodies[i].Velocity = glm::vec3(0.0f);
        }

        pc.IsRunning    = running;
        pc.IsCrouching  = crouching;
        pc.LastMoveAxis = axis;

        // ---- Headbob ----
        if (pc.HeadbobEnabled && pc.HeadbobCameraEntity >= 0 &&
            pc.HeadbobCameraEntity < (int)m_Scene.Transforms.size())
        {
            int camEnt = pc.HeadbobCameraEntity;
            // Capture rest Y on first move
            if (!pc._HeadbobBaseSet)
            {
                pc._HeadbobBaseY  = m_Scene.Transforms[camEnt].Position.y;
                pc._HeadbobBaseX  = m_Scene.Transforms[camEnt].Position.x;
                pc._HeadbobBaseSet = true;
            }

            bool isMovingNow = (glm::length(axis) > 0.01f);
            float targetBlend = isMovingNow ? 1.0f : 0.0f;
            // Smooth blend in/out (0.12s blend-in, 0.20s blend-out)
            float blendRate = isMovingNow ? (dt / 0.12f) : (dt / 0.20f);
            pc._HeadbobBlend += (targetBlend - pc._HeadbobBlend) * std::min(blendRate, 1.0f);

            if (pc._HeadbobBlend > 0.001f)
            {
                // Advance phase by distance walked
                float distWalked = glm::length(worldMove);
                pc._HeadbobPhase += distWalked * pc.HeadbobFrequency * (float)(2.0 * M_PI);

                // Vertical bob: sin²(phase/2) pattern (smoother than raw sin)
                float bobY = sinf(pc._HeadbobPhase) * pc.HeadbobAmplitudeY * pc._HeadbobBlend;
                // Lateral sway: half frequency
                float swayX = sinf(pc._HeadbobPhase * 0.5f) * pc.HeadbobAmplitudeX * pc._HeadbobBlend;

                m_Scene.Transforms[camEnt].Position.y = pc._HeadbobBaseY + bobY;
                m_Scene.Transforms[camEnt].Position.x = pc._HeadbobBaseX + swayX * 0.25f;
            }
            else
            {
                // Smoothly return to rest position
                float restY = pc._HeadbobBaseY;
                float curY  = m_Scene.Transforms[camEnt].Position.y;
                m_Scene.Transforms[camEnt].Position.y = curY + (restY - curY) * std::min(dt / 0.08f, 1.0f);
                float restX = pc._HeadbobBaseX;
                float curX  = m_Scene.Transforms[camEnt].Position.x;
                m_Scene.Transforms[camEnt].Position.x = curX + (restX - curX) * std::min(dt / 0.08f, 1.0f);
            }
        }
    }
}

void Application::UpdateMouseLook(float dt)
{
    (void)dt;
    const MouseDelta delta = InputManager::GetMouseDelta();
    if (delta.x == 0.0f && delta.y == 0.0f) return;

    const int n = (int)m_Scene.MouseLooks.size();
    for (int i = 0; i < n; ++i)
    {
        auto& ml = m_Scene.MouseLooks[i];
        if (!ml.Active || !ml.Enabled) continue;

        float dx = delta.x * ml.SensitivityX * (ml.InvertX ? -1.0f :  1.0f);
        float dy = delta.y * ml.SensitivityY * (ml.InvertY ? -1.0f :  1.0f);

        // --- Yaw: rotate the yaw-target entity around world Y ---
        int yawEnt = ml.YawTargetEntity;
        if (yawEnt >= 0 && yawEnt < (int)m_Scene.Transforms.size())
        {
            ml.CurrentYaw += dx;
            m_Scene.Transforms[yawEnt].Rotation.y = ml.CurrentYaw;
        }

        // --- Pitch: rotate the pitch-target entity to look up/down ---
        // For an X-forward entity the Z-rotation tilts the nose up/down in the XY
        // plane (true pitch).  X-rotation would roll the entity sideways instead.
        int pitchEnt = ml.PitchTargetEntity;
        if (pitchEnt >= 0 && pitchEnt < (int)m_Scene.Transforms.size())
        {
            ml.CurrentPitch -= dy;   // subtract: mouse up → positive tilt → look up
            if (ml.ClampPitch)
            {
                if (ml.CurrentPitch < ml.PitchMin) ml.CurrentPitch = ml.PitchMin;
                if (ml.CurrentPitch > ml.PitchMax) ml.CurrentPitch = ml.PitchMax;
            }
            // Z-axis rotation = pitch for an entity whose local forward is +X
            m_Scene.Transforms[pitchEnt].Rotation.x = ml.CurrentPitch;
        }
    }
}

// ----------------------------------------------------------------
// UpdateTriggers — AABB overlap detection, fires channels on enter/exit
// ----------------------------------------------------------------
void Application::UpdateTriggers(float dt)
{
    (void)dt;
    // Collect player world positions
    struct PlayerInfo { glm::vec3 pos; glm::vec3 half; };
    std::vector<PlayerInfo> players;
    for (int i = 0; i < (int)m_Scene.PlayerControllers.size(); ++i)
    {
        const auto& pc = m_Scene.PlayerControllers[i];
        if (!pc.Active || !pc.Enabled) continue;
        glm::vec3 pos  = m_Scene.GetEntityWorldPos(i);
        // Player AABB: half-extents based on rigidbody collider if available
        glm::vec3 half = glm::vec3(0.35f, 0.9f, 0.35f);
        if (i < (int)m_Scene.RigidBodies.size() && m_Scene.RigidBodies[i].HasColliderModule)
        {
            const auto& rb = m_Scene.RigidBodies[i];
            if (rb.Collider == ColliderType::Box)
                half = rb.ColliderSize * 0.5f;
            else
                half = glm::vec3(rb.ColliderRadius, rb.ColliderHeight * 0.5f, rb.ColliderRadius);
        }
        players.push_back({ pos, half });
    }

    for (int i = 0; i < (int)m_Scene.Triggers.size(); ++i)
    {
        auto& tr = m_Scene.Triggers[i];
        if (!tr.Active || !tr.Enabled) continue;

        glm::vec3 center = m_Scene.GetEntityWorldPos(i) + tr.Offset;

        bool anyPlayerInside = false;
        for (const auto& pl : players)
        {
            // AABB overlap test (trigger half-size == tr.Size already represents half-extents)
            glm::vec3 delta = glm::abs(pl.pos - center);
            if (delta.x <= tr.Size.x + pl.half.x &&
                delta.y <= tr.Size.y + pl.half.y &&
                delta.z <= tr.Size.z + pl.half.z)
            {
                anyPlayerInside = true;
                break;
            }
        }

        bool wasInside = tr._PlayerInside;
        tr._PlayerInside = anyPlayerInside;

        if (!wasInside && anyPlayerInside)
        {
            // Enter event
            if (!tr.OneShot || !tr._HasFired)
            {
                if (tr.Channel >= 0)
                    m_Scene.SetChannelBool(tr.Channel, tr.InsideValue);
                tr._HasFired = true;
                LuaSystem::FireTriggerEnter(i);
            }
        }
        else if (wasInside && !anyPlayerInside)
        {
            // Exit event
            if (tr.Channel >= 0)
                m_Scene.SetChannelBool(tr.Channel, tr.OutsideValue);
            LuaSystem::FireTriggerExit(i);
        }
    }
}

// ----------------------------------------------------------------
// UpdateAnimators — advance keyframe animations
// ----------------------------------------------------------------
void Application::UpdateAnimators(float dt)
{
    auto applyEasing = [](float t, AnimatorEasing e) -> float {
        switch (e) {
            case AnimatorEasing::EaseIn:    return t * t;
            case AnimatorEasing::EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
            case AnimatorEasing::EaseInOut: return t < 0.5f ? 2.0f*t*t : 1.0f - (-2.0f*t+2.0f)*(-2.0f*t+2.0f)*0.5f;
            default:                        return t;
        }
    };

    for (int i = 0; i < (int)m_Scene.Animators.size(); ++i)
    {
        auto& an = m_Scene.Animators[i];
        if (!an.Active || !an.Enabled) continue;

        // Auto-play on first frame if AutoPlay is set and not yet started
        if (an.AutoPlay && !an.Playing && !an._DelayDone)
            an.Playing = true;

        if (!an.Playing) continue;

        // Delay
        if (!an._DelayDone)
        {
            an._Timer += dt * std::max(0.0f, an.PlaybackSpeed);
            if (an._Timer < an.Delay) continue;
            an._Timer = 0.0f;
            an._DelayDone = true;
        }

        float dur = std::max(an.Duration, 0.001f);
        an._Timer += dt * std::max(0.0f, an.PlaybackSpeed);
        float rawT = std::min(an._Timer / dur, 1.0f);

        // Handle oscillation direction
        float phaseT;
        if (an.Mode == AnimatorMode::Oscillate)
        {
            if (an._Dir > 0.0f)
                phaseT = rawT;
            else
                phaseT = 1.0f - rawT;
        }
        else
        {
            phaseT = rawT;
        }

        float easedT = applyEasing(phaseT, an.Easing);
        glm::vec3 target = glm::mix(an.From, an.To, easedT);

        // Apply to entity transform
        if (i < (int)m_Scene.Transforms.size())
        {
            auto& tr = m_Scene.Transforms[i];
            float w = glm::clamp(an.BlendWeight, 0.0f, 1.0f);
            if (an.Property == AnimatorProperty::Position)
                tr.Position = glm::mix(tr.Position, target, w);
            else if (an.Property == AnimatorProperty::Rotation)
                tr.Rotation = glm::mix(tr.Rotation, target, w);
            else if (an.Property == AnimatorProperty::Scale)
                tr.Scale    = glm::mix(tr.Scale,    target, w);
        }

        // Advance and handle end-of-cycle
        if (an._Timer >= dur)
        {
            an._Timer = 0.0f;
            if (an.Mode == AnimatorMode::Oscillate)
            {
                an._Dir *= -1.0f;  // reverse direction
            }
            else if (an.Mode == AnimatorMode::OneShot)
            {
                an.Playing = false;
            }
            // Loop: just restart (timer already reset)
        }
    }
}

// ----------------------------------------------------------------
// UpdateAudio — simple 3D attenuation + barrier occlusion
// ----------------------------------------------------------------
void Application::UpdateAudio(float dt)
{
    (void)dt;
    // Camera position for listener
    int camIdx = m_Scene.GetActiveGameCamera();
    glm::vec3 listenerPos = (camIdx >= 0) ? m_Scene.GetEntityWorldPos(camIdx) : m_EditorCamera.GetPosition();
    for (int i = 0; i < (int)m_Scene.AudioSources.size(); ++i)
    {
        auto& src = m_Scene.AudioSources[i];
        if (!src.Active || !src.Enabled) continue;
        if (src.TriggerOneShot)
        {
            bool gateOk = true;
            if (src.GateChannel >= 0)
            {
                bool gv = m_Scene.GetChannelBool(src.GateChannel, src.GateValue);
                gateOk = (gv == src.GateValue);
            }
            if (gateOk && !src._GatePrev)
            {
                src._Playing = true;
                src._GatePrev = true;
            }
            else if (!gateOk)
            {
                src._GatePrev = false;
            }
        }
        glm::vec3 pos = m_Scene.GetEntityWorldPos(i);
        float d = glm::length(pos - listenerPos);
        float gain = 0.0f;
        if (src.Spatial)
        {
            if (d <= src.MinDistance) gain = 1.0f;
            else if (d >= src.MaxDistance) gain = 0.0f;
            else gain = 1.0f - (d - src.MinDistance) / std::max(0.001f, (src.MaxDistance - src.MinDistance));
        }
        else gain = 1.0f;
        // Occlusion by barriers (simple AABB ray test accumulating occlusion)
        float occAccum = 0.0f;
        if (d > 0.001f) // skip occlusion when source == listener
        {
            glm::vec3 dir = (listenerPos - pos) / d; // already know length = d
            for (int bi = 0; bi < (int)m_Scene.AudioBarriers.size(); ++bi)
            {
                const auto& b = m_Scene.AudioBarriers[bi];
                if (!b.Active || !b.Enabled) continue;
                glm::vec3 bc = m_Scene.GetEntityWorldPos(bi) + b.Offset;
                glm::vec3 half = b.Size;
                // Slab intersection test (signed invD for correct interval sorting)
                glm::vec3 invD;
                invD.x = (std::abs(dir.x) > 1e-6f) ? (1.0f / dir.x) : (dir.x >= 0 ? 1e6f : -1e6f);
                invD.y = (std::abs(dir.y) > 1e-6f) ? (1.0f / dir.y) : (dir.y >= 0 ? 1e6f : -1e6f);
                invD.z = (std::abs(dir.z) > 1e-6f) ? (1.0f / dir.z) : (dir.z >= 0 ? 1e6f : -1e6f);
                glm::vec3 t0 = (bc - half - pos) * invD;
                glm::vec3 t1 = (bc + half - pos) * invD;
                float tmin = std::max(std::max(std::min(t0.x, t1.x), std::min(t0.y, t1.y)), std::min(t0.z, t1.z));
                float tmax = std::min(std::min(std::max(t0.x, t1.x), std::max(t0.y, t1.y)), std::max(t0.z, t1.z));
                if (tmax >= std::max(0.0f, tmin) && tmin < d)
                    occAccum += b.Occlusion;
            }
        }
        gain *= std::max(0.0f, 1.0f - glm::clamp(occAccum, 0.0f, 1.0f) * src.OcclusionFactor);
        src._CurrentGain = gain * src.Volume;
        if (src.OutputChannel >= 0)
            m_Scene.SetChannelFloat(src.OutputChannel, src._CurrentGain);
    }
}
// ----------------------------------------------------------------
// UpdateAnimationControllers — apply state machines to Animators
// ----------------------------------------------------------------
void Application::UpdateAnimationControllers(float dt)
{
    for (int i = 0; i < (int)m_Scene.AnimationControllers.size(); ++i)
    {
        auto& ac = m_Scene.AnimationControllers[i];
        if (!ac.Active || !ac.Enabled) continue;
        if (ac._CurrentState < 0)
        {
            if (ac.AutoStart && ac.DefaultState >= 0 && ac.DefaultState < (int)ac.States.size())
            {
                ac._CurrentState = ac.DefaultState;
                ac._StateTime = 0.0f;
            }
            else continue;
        }
        ac._StateTime += dt;
        // Evaluate transitions
        int nextState = ac._CurrentState;
        for (const auto& tr : ac.Transitions)
        {
            if (tr.FromState != ac._CurrentState) continue;
            if (tr.ExitTime > 0.0f && ac._StateTime < tr.ExitTime) continue;
            bool cond = false;
            if (tr.Channel >= 0)
            {
                switch (tr.Condition)
                {
                    case AnimationConditionOp::BoolTrue:   cond = m_Scene.GetChannelBool(tr.Channel, false); break;
                    case AnimationConditionOp::BoolFalse:  cond = !m_Scene.GetChannelBool(tr.Channel, false); break;
                    case AnimationConditionOp::FloatGreater: cond = (m_Scene.GetChannelFloat(tr.Channel, 0.0f) > tr.Threshold); break;
                    case AnimationConditionOp::FloatLess:    cond = (m_Scene.GetChannelFloat(tr.Channel, 0.0f) < tr.Threshold); break;
                }
            }
            if (cond) { nextState = tr.ToState; break; }
        }
        if (nextState != ac._CurrentState && nextState >= 0 && nextState < (int)ac.States.size())
        {
            ac._CurrentState = nextState;
            ac._StateTime = 0.0f;
            const auto& st = ac.States[nextState];
            // Apply to Animator component on same entity
            if (i < (int)m_Scene.Animators.size())
            {
                auto& an = m_Scene.Animators[i];
                an.Active    = true;
                an.Enabled   = true;
                an.Playing   = st.AutoPlay;
                an.AutoPlay  = st.AutoPlay;
                an.Property  = st.Property;
                an.Mode      = st.Mode;
                an.Easing    = st.Easing;
                an.From      = st.From;
                an.To        = st.To;
                an.Duration  = st.Duration;
                an.Delay     = st.Delay;
                an.PlaybackSpeed = st.PlaybackSpeed;
                an.BlendWeight   = st.BlendWeight;
                an._Timer    = 0.0f;
                an._DelayDone = false;
                an._Phase   = 0.0f;
            }
        }
    }
}
int Application::RaycastScene(float mx, float my, int winW, int winH)
{
    float aspect  = (float)winW / (float)winH;
    glm::mat4 vp  = m_EditorCamera.GetProjection(aspect) * m_EditorCamera.GetViewMatrix();
    glm::mat4 vpI = glm::inverse(vp);

    float ndcX =  2.0f * mx / (float)winW - 1.0f;
    float ndcY = -2.0f * my / (float)winH + 1.0f;

    glm::vec4 nearH = vpI * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = vpI * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    nearH /= nearH.w;
    farH  /= farH.w;

    glm::vec3 orig = glm::vec3(nearH);
    glm::vec3 dir  = glm::normalize(glm::vec3(farH - nearH));

    int   best = -1;
    float minT = 1e30f;

    for (size_t i = 0; i < m_Scene.Transforms.size(); i++)
    {
        bool hasMesh   = m_Scene.MeshRenderers[i].MeshPtr != nullptr;
        bool hasCamera = m_Scene.GameCameras[i].Active;
        if (!hasMesh && !hasCamera) continue;

        glm::vec3 pos  = m_Scene.GetEntityWorldPos((int)i);
        glm::vec3 half;
        if (hasCamera) {
            half = glm::vec3(0.3f);
        } else if (m_Scene.MeshRenderers[i].MeshPtr) {
            // Use actual mesh AABB scaled by entity scale
            const Mesh* mp = m_Scene.MeshRenderers[i].MeshPtr;
            glm::vec3 s = m_Scene.Transforms[i].Scale;
            glm::vec3 extent = (mp->BoundsMax - mp->BoundsMin) * 0.5f;
            half = extent * s;
        } else {
            half = m_Scene.Transforms[i].Scale * 0.5f;
        }
        glm::vec3 bmin = pos - half;
        glm::vec3 bmax = pos + half;

        // Ray-AABB slab method
        float tmin = -1e30f, tmax = 1e30f;
        bool  miss = false;
        for (int ax = 0; ax < 3; ax++)
        {
            float orig_ax = (&orig.x)[ax];
            float dir_ax  = (&dir.x)[ax];
            float lo      = (&bmin.x)[ax];
            float hi      = (&bmax.x)[ax];

            if (std::abs(dir_ax) < 1e-6f)
            {
                if (orig_ax < lo || orig_ax > hi) { miss = true; break; }
            }
            else
            {
                float t1 = (lo - orig_ax) / dir_ax;
                float t2 = (hi - orig_ax) / dir_ax;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) { miss = true; break; }
            }
        }

        if (!miss && tmin > 0.0f && tmin < minT)
        {
            minT = tmin;
            best = (int)i;
        }
    }
    return best;
}

// ----------------------------------------------------------------
// UpdateSkinnedMeshes — CPU skinning using loaded skeleton weights
// ----------------------------------------------------------------
void Application::UpdateSkinnedMeshes(float dt)
{
    (void)dt;
    for (int i = 0; i < (int)m_Scene.SkinnedMeshes.size(); ++i)
    {
        auto& sk = m_Scene.SkinnedMeshes[i];
        if (!sk.Active || !sk.Enabled) continue;
        if (i >= (int)m_Scene.MeshRenderers.size()) continue;
        Mesh* mesh = m_Scene.MeshRenderers[i].MeshPtr;
        if (!mesh) continue;
        if (!sk.Loaded || sk.BoneCount <= 0) continue;
        if (sk.BaseVertices.size() != mesh->GetCpuVertices().size())
            sk.BaseVertices = mesh->GetCpuVertices();
        std::vector<Mesh::CpuVertex> deformed = sk.BaseVertices;
        // Compute skinning: for each vertex apply weighted bone matrices
        for (size_t vi = 0; vi < deformed.size(); ++vi)
        {
            glm::vec4 pos = glm::vec4(sk.BaseVertices[vi].pos, 1.0f);
            glm::vec3 nor = sk.BaseVertices[vi].normal;
            glm::ivec4 bi = (vi < sk.BoneIndices.size()) ? sk.BoneIndices[vi] : glm::ivec4(0);
            glm::vec4  bw = (vi < sk.BoneWeights.size()) ? sk.BoneWeights[vi] : glm::vec4(1,0,0,0);
            glm::mat4 m0 = (bi.x >= 0 && bi.x < sk.BoneCount) ? sk.Pose[bi.x] : glm::mat4(1.0f);
            glm::mat4 m1 = (bi.y >= 0 && bi.y < sk.BoneCount) ? sk.Pose[bi.y] : glm::mat4(1.0f);
            glm::mat4 m2 = (bi.z >= 0 && bi.z < sk.BoneCount) ? sk.Pose[bi.z] : glm::mat4(1.0f);
            glm::mat4 m3 = (bi.w >= 0 && bi.w < sk.BoneCount) ? sk.Pose[bi.w] : glm::mat4(1.0f);
            glm::vec4 p2 = m0 * pos * bw.x + m1 * pos * bw.y + m2 * pos * bw.z + m3 * pos * bw.w;
            glm::vec3 n2 = glm::mat3(m0) * nor * bw.x + glm::mat3(m1) * nor * bw.y + glm::mat3(m2) * nor * bw.z + glm::mat3(m3) * nor * bw.w;
            deformed[vi].pos    = glm::vec3(p2);
            deformed[vi].normal = glm::normalize(n2);
        }
        mesh->UpdateCpuVertices(deformed);
    }
}

bool Application::LoadSkeletonForEntity(int idx, const std::string& path)
{
    if (idx < 0 || idx >= (int)m_Scene.SkinnedMeshes.size()) return false;
    auto& sk = m_Scene.SkinnedMeshes[idx];
    sk.SkeletonPath = path;
    sk.Active = true;
    sk.Enabled = true;
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    int stage = 0;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#') continue;
        if (line == "[bones]") { stage = 1; continue; }
        if (line == "[bindpose]") { stage = 2; continue; }
        if (line == "[weights]") { stage = 3; continue; }
        std::istringstream ss(line);
        if (stage == 1)
        {
            std::string name; int parent;
            ss >> name >> parent;
            sk.Parent.push_back(parent);
            sk.BoneCount++;
        }
        else if (stage == 2)
        {
            glm::mat4 m(1.0f);
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    ss >> m[c][r];
            sk.BindPose.push_back(m);
            sk.Pose.push_back(m);
        }
        else if (stage == 3)
        {
            int vi; int i0,i1,i2,i3; float w0,w1,w2,w3;
            ss >> vi >> i0 >> w0 >> i1 >> w1 >> i2 >> w2 >> i3 >> w3;
            if ((size_t)vi >= sk.BoneIndices.size())
            {
                sk.BoneIndices.resize(vi + 1, glm::ivec4(0));
                sk.BoneWeights.resize(vi + 1, glm::vec4(1,0,0,0));
            }
            sk.BoneIndices[vi] = glm::ivec4(i0,i1,i2,i3);
            sk.BoneWeights[vi] = glm::vec4(w0,w1,w2,w3);
        }
    }
    sk.Loaded = (sk.BoneCount > 0 && !sk.BindPose.empty());
    return sk.Loaded;
}
void Application::HandleToolbarInput()
{
    // ImGui play/pause buttons (polled from UIManager)
    if (m_UIManager.HasPendingPlayToggle())
    {
        m_UIManager.ConsumePlayToggle();
        if (m_Mode == EngineMode::Editor)
        {
            SaveEditorState();
            m_Mode = EngineMode::Game;
            SpawnMultiplayerPlayers();
            LuaSystem::SetWindow(m_Window.GetNativeWindow());
            LuaSystem::Init(m_Scene);
            LuaSystem::StartScripts();
        }
        else
        {
            LuaSystem::Shutdown();
            DespawnMultiplayerPlayers();
            RestoreEditorState();
            m_Mode = EngineMode::Editor;
        }
    }

    if (m_UIManager.HasPendingPauseToggle())
    {
        m_UIManager.ConsumePauseToggle();
        if (m_Mode == EngineMode::Game)   m_Mode = EngineMode::Paused;
        else if (m_Mode == EngineMode::Paused) m_Mode = EngineMode::Game;
    }

    // Atalho de teclado: F5 = play/stop, F6 = pause
    if (InputManager::IsKeyDown(Key::F1))
    {
        if (m_Mode == EngineMode::Editor)
        {
            SaveEditorState();
            m_Mode = EngineMode::Game;
            SpawnMultiplayerPlayers();
            LuaSystem::SetWindow(m_Window.GetNativeWindow());
            LuaSystem::Init(m_Scene);
            LuaSystem::StartScripts();
        }
        else
        {
            LuaSystem::Shutdown();
            DespawnMultiplayerPlayers();
            RestoreEditorState();
            m_Mode = EngineMode::Editor;
        }
    }
    if (InputManager::IsKeyDown(Key::F2))
    {
        if (m_Mode == EngineMode::Game)   m_Mode = EngineMode::Paused;
        else if (m_Mode == EngineMode::Paused) m_Mode = EngineMode::Game;
    }

    // Ctrl+S = Save project
    {
        bool ctrlHeld = InputManager::IsKeyPressed(Key::LeftControl) ||
                        InputManager::IsKeyPressed(Key::RightControl);
        if (ctrlHeld && InputManager::IsKeyDown(Key::S))
            SaveProject();
    }
}

void Application::Run()
{
    Renderer::Init();

    // ---- Detect standalone game mode ----
    m_GameModeOnly = std::filesystem::exists("game.mode");
    if (m_GameModeOnly)
    {
        // Parse title from marker
        std::ifstream mk("game.mode");
        std::string line;
        while (std::getline(mk, line))
        {
            if (line.rfind("title=", 0) == 0)
            {
                std::string title = line.substr(6);
                glfwSetWindowTitle(m_Window.GetNativeWindow(), title.c_str());
                m_ProjectName = title;
            }
        }

        // Go borderless-fullscreen on the primary monitor
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* mode    = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(m_Window.GetNativeWindow(), monitor, 0, 0,
                             mode->width, mode->height, mode->refreshRate);

        // Load the game scene
        if (std::filesystem::exists("assets/scenes/default.tscene"))
            SceneSerializer::Load(m_Scene, "assets/scenes/default.tscene");

        // Hide cursor (game controls mouse cursor)
        glfwSetInputMode(m_Window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        // Start Lua scripts
        LuaSystem::SetWindow(m_Window.GetNativeWindow());
        LuaSystem::Init(m_Scene);
        LuaSystem::StartScripts();
        m_Mode = EngineMode::Game;

        // ---- Standalone game loop (no editor UI) ----
        while (!m_Window.ShouldClose())
        {
            float time      = (float)glfwGetTime();
            float deltaTime = time - m_LastFrameTime;
            m_LastFrameTime = time;
            if (deltaTime > 0.05f) deltaTime = 0.05f;

            InputManager::Update(m_Window.GetNativeWindow());

            // Quit on Escape
            if (InputManager::IsKeyDown(Key::Escape))
                glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);

            // Game logic
            UpdatePlayerControllers(deltaTime);
            UpdateMouseLook(deltaTime);
            MultiplayerSystem::Update(m_Scene, deltaTime);
            UpdateTriggers(deltaTime);
            UpdateAnimators(deltaTime);
            LuaSystem::UpdateScripts(m_Scene, deltaTime);

            // Handle Lua scene.loadScene() request (standalone game mode)
            {
                std::string pendingScene = LuaSystem::GetPendingLoadScene();
                if (!pendingScene.empty()) {
                    namespace fs = std::filesystem;
                    LuaSystem::Shutdown();
                    for (auto& mr : m_Scene.MeshRenderers)
                        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
                    m_Scene = Scene{};
                    std::string fullPath = pendingScene;
                    if (!fs::path(pendingScene).is_absolute())
                        fullPath = (std::string("assets/scenes/") + pendingScene);
                    if (fs::exists(fullPath))
                        SceneSerializer::Load(m_Scene, fullPath);
                    LuaSystem::SetWindow(m_Window.GetNativeWindow());
                    LuaSystem::Init(m_Scene);
                    LuaSystem::StartScripts();
                }
            }

            m_Scene.OnUpdate(deltaTime);
            PhysicsSystem::Update(m_Scene, deltaTime);
            MazeGenerator::Update(m_Scene, deltaTime);

            int winW = m_Window.GetWidth();
            int winH = m_Window.GetHeight();

            Renderer::BeginFrame();

            // Render from game camera
            int camIdx = m_Scene.GetActiveGameCamera();
            if (camIdx >= 0)
            {
                Renderer::RenderSceneGame(m_Scene, winW, winH);
            }
            else
            {
                // No camera — clear to black
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            m_Window.Update();
        }

        LuaSystem::Shutdown();
        LightmapManager::Instance().ReleaseAll();
        return;  // skip the editor run loop below
    }

    // ---- Editor mode ----
    m_UIManager.Init(m_Window.GetNativeWindow());

    // Load default scene if present (no project open yet)
    {
        namespace fs = std::filesystem;
        const std::string defScene = "assets/scenes/default.tscene";
        if (m_CurrentScenePath.empty() && fs::exists(defScene)) {
            SceneSerializer::Load(m_Scene, defScene);
            m_EditorCamera.SetPosition(m_Scene.EditorCamPos);
            m_EditorCamera.SetYaw(m_Scene.EditorCamYaw);
            m_EditorCamera.SetPitch(m_Scene.EditorCamPitch);
            m_CurrentScenePath = fs::absolute(defScene).string();
        }
        RefreshSceneFiles();
    }

    // Wire up the Win32 timer render callback so the splash animation
    // keeps playing while the user drags the title bar.
    m_Window.SetRenderCallback([&]() {
        int w = m_Window.GetWidth(), h = m_Window.GetHeight();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        m_UIManager.BeginFrame();
        std::vector<std::string> noDrops;
        m_UIManager.Render(m_Scene, m_SelectedEntity, w, h, noDrops, m_GizmoMode);
        m_UIManager.EndFrame();
        glfwSwapBuffers(m_Window.GetNativeWindow());
    });

    // Register callback for external file drag & drop
    glfwSetDropCallback(m_Window.GetNativeWindow(), OnGLFWDrop);

    while (!m_Window.ShouldClose())
    {
        float time      = (float)glfwGetTime();
        float deltaTime = time - m_LastFrameTime;
        m_LastFrameTime = time;
        if (deltaTime > 0.05f) deltaTime = 0.05f; // cap at 20fps

        InputManager::Update(m_Window.GetNativeWindow());
        m_UIManager.BeginFrame();
        HandleToolbarInput();

        // ---- Poll project operation requests from UIManager ----
        if (m_UIManager.HasPendingNewProject()) {
            std::string name, parent;
            m_UIManager.ConsumeNewProject(name, parent);
            if (!name.empty()) NewProject(name, parent.empty() ? "." : parent);
        }
        if (m_UIManager.HasPendingOpenProject()) {
            std::string path;
            m_UIManager.ConsumeOpenProject(path);
            if (!path.empty()) OpenProject(path);
        }
        if (m_UIManager.HasPendingSaveProject()) {
            m_UIManager.ConsumeSaveProject();
            SaveProject();
        }
        if (m_UIManager.HasPendingExportGame()) {
            std::string outPath, gameName;
            m_UIManager.ConsumeExportGame(outPath, gameName);
            if (!outPath.empty()) ExportGame(outPath, gameName);
        }
#ifdef _WIN32
        if (m_UIManager.HasPendingLaunchMpHost()) {
            std::string nick; int port = 27015;
            m_UIManager.ConsumePendingLaunchMpHost(nick, port);
            char exeBuf[4096] = {};
            GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf));
            std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
            std::filesystem::path gameExe = exeDir / "_engine" / "GameRuntime.exe";
            if (!std::filesystem::exists(gameExe)) gameExe = exeDir / "GameRuntime.exe";
            std::string cmd = "\"" + gameExe.string() + "\" --mp-mode=host --mp-port=" + std::to_string(port) + " --mp-nick=\"" + nick + "\"";
            LaunchGameRuntimeProcess(cmd);
        }
        if (m_UIManager.HasPendingLaunchMpClient()) {
            std::string server, nick; int port = 27015;
            m_UIManager.ConsumePendingLaunchMpClient(server, nick, port);
            char exeBuf[4096] = {};
            GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf));
            std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
            std::filesystem::path gameExe = exeDir / "_engine" / "GameRuntime.exe";
            if (!std::filesystem::exists(gameExe)) gameExe = exeDir / "GameRuntime.exe";
            std::string cmd = "\"" + gameExe.string() + "\" --mp-mode=client --mp-server=" + server + " --mp-port=" + std::to_string(port) + " --mp-nick=\"" + nick + "\"";
            LaunchGameRuntimeProcess(cmd);
        }
        if (m_UIManager.HasPendingLaunchMpPair()) {
            std::string hostNick, clientNick; int port = 27015;
            m_UIManager.ConsumePendingLaunchMpPair(hostNick, clientNick, port);
            // Auto-save current scene so both GameRuntime windows load the latest version
            if (!m_CurrentScenePath.empty()) {
                m_Scene.EditorCamPos   = m_EditorCamera.GetPosition();
                m_Scene.EditorCamYaw   = m_EditorCamera.GetYaw();
                m_Scene.EditorCamPitch = m_EditorCamera.GetPitch();
                SceneSerializer::Save(m_Scene, m_CurrentScenePath);
            }
            char exeBuf[4096] = {};
            GetModuleFileNameA(nullptr, exeBuf, sizeof(exeBuf));
            std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();
            std::filesystem::path gameExe = exeDir / "_engine" / "GameRuntime.exe";
            if (!std::filesystem::exists(gameExe)) gameExe = exeDir / "GameRuntime.exe";
            std::string sceneFlag = m_CurrentScenePath.empty() ? "" : " --scene=\"" + m_CurrentScenePath + "\"";
            std::string hostCmd = "\"" + gameExe.string() + "\" --windowed --mp-mode=host --mp-port=" + std::to_string(port) + " --mp-nick=\"" + hostNick + "\"" + sceneFlag;
            std::string clientCmd = "\"" + gameExe.string() + "\" --windowed --mp-mode=client --mp-server=127.0.0.1 --mp-port=" + std::to_string(port) + " --mp-nick=\"" + clientNick + "\"" + sceneFlag;
            LaunchGameRuntimeProcess(hostCmd);
            LaunchGameRuntimeProcess(clientCmd);
        }
#endif

        // ---- Poll scene operation requests from UIManager ----
        if (m_UIManager.HasPendingNewScene()) {
            std::string sname;
            m_UIManager.ConsumeNewScene(sname);
            if (!sname.empty()) NewScene(sname);
        }
        if (m_UIManager.HasPendingOpenScene()) {
            std::string spath;
            m_UIManager.ConsumeOpenScene(spath);
            if (!spath.empty()) OpenScene(spath);
        }
        if (m_UIManager.HasPendingDeleteScene()) {
            std::string spath;
            m_UIManager.ConsumeDeleteScene(spath);
            if (!spath.empty()) DeleteScene(spath);
        }

        // s_DroppedFiles is processed in UIManager::Render (which knows the current folder)
        Renderer::BeginFrame();

        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        // Game logic (only when actually running)
        if (m_Mode == EngineMode::Game)
        {
            UpdatePlayerControllers(deltaTime);
            UpdateMouseLook(deltaTime);
            MultiplayerSystem::Update(m_Scene, deltaTime);
            UpdateTriggers(deltaTime);
            UpdateAnimationControllers(deltaTime);
            UpdateAnimators(deltaTime);
            UpdateSkinnedMeshes(deltaTime);
            UpdateAudio(deltaTime);
            LuaSystem::UpdateScripts(m_Scene, deltaTime);

            // Handle Lua scene.loadScene() request (editor game mode)
            {
                std::string pendingScene = LuaSystem::GetPendingLoadScene();
                if (!pendingScene.empty()) {
                    namespace fs = std::filesystem;
                    LuaSystem::Shutdown();
                    RestoreEditorState();
                    for (auto& mr : m_Scene.MeshRenderers)
                        if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }
                    m_Scene = Scene{};
                    std::string fullPath = pendingScene;
                    if (!fs::path(pendingScene).is_absolute() && !m_ProjectFolder.empty())
                        fullPath = m_ProjectFolder + "/assets/scenes/" + pendingScene;
                    if (fs::exists(fullPath)) {
                        SceneSerializer::Load(m_Scene, fullPath);
                        m_EditorCamera.SetPosition(m_Scene.EditorCamPos);
                        m_EditorCamera.SetYaw(m_Scene.EditorCamYaw);
                        m_EditorCamera.SetPitch(m_Scene.EditorCamPitch);
                        m_CurrentScenePath = fs::absolute(fullPath).string();
                    }
                    RefreshSceneFiles();
                    LuaSystem::SetWindow(m_Window.GetNativeWindow());
                    LuaSystem::Init(m_Scene);
                    LuaSystem::StartScripts();
                }
            }

            m_Scene.OnUpdate(deltaTime);
            PhysicsSystem::Update(m_Scene, deltaTime);
            MazeGenerator::Update(m_Scene, deltaTime);
        }
        else
        {
            m_Scene.OnUpdate(deltaTime);
        }

        // Determine which view to render in the viewport
        bool showGameView      = (m_UIManager.GetViewportTab() == 1);
        bool showProjectView   = (m_UIManager.GetViewportTab() == 2);
        bool showPrefabEditor  = m_UIManager.IsPrefabEditorActive();
        bool showMazeOverview  = (m_UIManager.GetViewportTab() == 4);
        bool showRoomEditor    = (m_UIManager.GetViewportTab() == 5);

        // Editor camera update: scene/prefab use m_EditorCamera; room editor uses its own camera
        if (!showGameView && !showProjectView && !showMazeOverview)
        {
            if (showRoomEditor)
                m_RoomEditorCamera.OnUpdate(deltaTime);
            else
                m_EditorCamera.OnUpdate(deltaTime);
        }

        // Prefab editor: rebuild preview scene when the edited prefab changes
        if (showPrefabEditor)
        {
            int editIdx = m_UIManager.GetEditingPrefabIdx();
            if (editIdx != m_PrefabPreviewIdx || m_PrefabPreviewIdx < 0)
                RebuildPrefabPreview();
            else if (m_PrefabPreviewIdx >= 0 && m_PrefabPreviewIdx < (int)m_Scene.Prefabs.size())
            {
                // Sync sidebar property edits → preview entities each frame
                const PrefabAsset& pf = m_Scene.Prefabs[m_PrefabPreviewIdx];
                for (int ni = 0; ni < (int)pf.Nodes.size(); ++ni)
                {
                    int ei = ni + 1; // offset for preview light
                    if (ei >= (int)m_PrefabPreviewScene.Transforms.size()) break;
                    m_PrefabPreviewScene.Transforms[ei] = pf.Nodes[ni].Transform;

                    // Sync light
                    if (pf.Nodes[ni].HasLight)
                    {
                        m_PrefabPreviewScene.Lights[ei] = pf.Nodes[ni].Light;
                        m_PrefabPreviewScene.Lights[ei].Active  = true;
                        m_PrefabPreviewScene.Lights[ei].Enabled = pf.Nodes[ni].Light.Enabled;
                    }
                    else
                    {
                        m_PrefabPreviewScene.Lights[ei].Active = false;
                    }

                    // Sync camera
                    if (pf.Nodes[ni].HasCamera)
                    {
                        m_PrefabPreviewScene.GameCameras[ei] = pf.Nodes[ni].Camera;
                        m_PrefabPreviewScene.GameCameras[ei].Active = true;
                    }
                    else
                    {
                        m_PrefabPreviewScene.GameCameras[ei].Active = false;
                    }

                    // Sync material by name
                    if (!pf.Nodes[ni].MaterialName.empty())
                    {
                        int matIdx = -1;
                        for (int mi = 0; mi < (int)m_PrefabPreviewScene.Materials.size(); ++mi)
                            if (m_PrefabPreviewScene.Materials[mi].Name == pf.Nodes[ni].MaterialName)
                                { matIdx = mi; break; }
                        m_PrefabPreviewScene.EntityMaterial[ei] = matIdx;
                    }
                    else
                    {
                        m_PrefabPreviewScene.EntityMaterial[ei] = -1;
                    }

                    // Sync mesh type
                    const std::string& mt = pf.Nodes[ni].MeshType;
                    if (mt != m_PrefabPreviewScene.MeshRenderers[ei].MeshType)
                    {
                        delete m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr;
                        m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr = nullptr;
                        m_PrefabPreviewScene.MeshRenderers[ei].MeshType.clear();
                        if (!mt.empty())
                        {
                            Mesh* mesh = nullptr;
                            if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
                            else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
                            else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
                            else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
                            else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
                            else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
                            if (mesh) {
                                m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr  = mesh;
                                m_PrefabPreviewScene.MeshRenderers[ei].MeshType = mt;
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // If we just left the prefab editor, drop the preview scene
            if (m_PrefabPreviewIdx >= 0)
            {
                FreeSceneMeshes(m_PrefabPreviewScene);
                m_PrefabPreviewScene = Scene();
                m_PrefabPreviewIdx = -1;
            }
        }

        // Room editor: rebuild preview scene when the edited room changes
        if (showRoomEditor)
        {
            int rsi = m_UIManager.GetEditingRoomSetIdx();
            int rri = m_UIManager.GetEditingRoomIdx();
            if (rsi != m_RoomPreviewSetIdx || rri != m_RoomPreviewRoomIdx)
                RebuildRoomPreview();
            else
            {
                if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                    rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
                {
                    const auto& room = m_Scene.RoomSets[rsi].Rooms[rri];
                    if ((int)room.Blocks.size() != m_RoomPreviewBlockCount ||
                        (int)room.Interior.size() != m_RoomPreviewInteriorCount)
                        RebuildRoomPreview();
                }
            }
        }
        else
        {
            if (m_RoomPreviewSetIdx >= 0)
            {
                if (!m_RoomPreviewLightmap.empty())
                {
                    LightmapManager::Instance().Release(m_RoomPreviewLightmap);
                    m_RoomPreviewLightmap.clear();
                }
                FreeSceneMeshes(m_RoomPreviewScene);
                m_RoomPreviewScene = Scene();
                m_RoomPreviewSetIdx = -1;
                m_RoomPreviewRoomIdx = -1;
                m_RoomPreviewBlockCount = -1;
                m_RoomPreviewInteriorCount = -1;
            }
        }

        // Editor picking/gizmo (only in editor mode + scene view)
        if (m_Mode == EngineMode::Editor && !showGameView && !showProjectView && !showPrefabEditor && !showRoomEditor && !showMazeOverview)
            HandleEditorInput(winW, winH);

        // Prefab editor gizmo
        if (showPrefabEditor)
            HandlePrefabEditorInput(winW, winH);

        // Room editor gizmo + entity selection
        if (showRoomEditor)
            HandleRoomEditorInput(winW, winH);

        // Room editor: handle 3D viewport click to place/remove blocks
        // Skip if gizmo is being dragged (user is manipulating an interior entity)
        if (showRoomEditor && m_UIManager.HasPendingRoomClick() && !m_Gizmo.IsDragging())
        {
            int clickY = 0;
            glm::vec2 ndc = m_UIManager.ConsumePendingRoomClick(clickY);

            // Reconstruct the ray from camera
            float vpW = (float)(winW - m_UIManager.GetLeftPanelWidth() - m_UIManager.GetRightPanelWidth());
            float vpH = (float)(winH - UIManager::k_AssetH - UIManager::k_TopStackH);
            float aspect = vpW / std::max(vpH, 1.0f);
            glm::mat4 proj = m_RoomEditorCamera.GetProjection(aspect);
            glm::mat4 view = m_RoomEditorCamera.GetViewMatrix();
            glm::mat4 invVP = glm::inverse(proj * view);

            glm::vec4 nearNDC(ndc.x, ndc.y, -1.0f, 1.0f);
            glm::vec4 farNDC (ndc.x, ndc.y,  1.0f, 1.0f);
            glm::vec4 nearW = invVP * nearNDC; nearW /= nearW.w;
            glm::vec4 farW  = invVP * farNDC;  farW  /= farW.w;
            glm::vec3 rayOrig(nearW);
            glm::vec3 rayDir = glm::normalize(glm::vec3(farW) - glm::vec3(nearW));

            // Intersect ray with Y = clickY plane
            float planeY = (float)clickY;
            if (std::abs(rayDir.y) > 0.0001f)
            {
                float t = (planeY - rayOrig.y) / rayDir.y;
                if (t > 0.0f)
                {
                    glm::vec3 hit = rayOrig + rayDir * t;
                    int gx = (int)std::round(hit.x);
                    int gz = (int)std::round(hit.z);

                    int rsi = m_UIManager.GetEditingRoomSetIdx();
                    int rri = m_UIManager.GetEditingRoomIdx();
                    if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                        rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
                    {
                        RoomTemplate& room = m_Scene.RoomSets[rsi].Rooms[rri];
                        bool shiftHeld = InputManager::IsKeyPressed(Key::LeftShift);
                        bool ctrlHeld  = InputManager::IsKeyPressed(Key::LeftControl);

                        // Find if a block exists at this grid position
                        int existIdx = -1;
                        for (int bi = 0; bi < (int)room.Blocks.size(); ++bi)
                        {
                            if (room.Blocks[bi].X == gx && room.Blocks[bi].Z == gz &&
                                room.Blocks[bi].Y == clickY)
                            {
                                existIdx = bi;
                                break;
                            }
                        }

                        if (m_UIManager.IsDoorMode())
                        {
                            // Door mode: only toggle doors on existing blocks
                            if (existIdx >= 0)
                            {
                                float fx = hit.x - (float)gx;
                                float fz = hit.z - (float)gz;
                                DoorFace df;
                                if (std::abs(fx) > std::abs(fz))
                                    df = (fx > 0) ? DoorFace::PosX : DoorFace::NegX;
                                else
                                    df = (fz > 0) ? DoorFace::PosZ : DoorFace::NegZ;

                                PushRoomEditState();
                                bool found = false;
                                for (int di = (int)room.Doors.size() - 1; di >= 0; --di)
                                {
                                    auto& d = room.Doors[di];
                                    if (d.BlockX == gx && d.BlockZ == gz && d.BlockY == clickY && d.Face == df)
                                    {
                                        room.Doors.erase(room.Doors.begin() + di);
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found)
                                {
                                    DoorPlacement dp;
                                    dp.BlockX = gx; dp.BlockZ = gz; dp.BlockY = clickY; dp.Face = df;
                                    room.Doors.push_back(dp);
                                }
                            }
                            // In door mode, clicking empty space does nothing
                        }
                        else if (ctrlHeld && shiftHeld)
                        {
                            // Ctrl+Shift = retract: remove all edge blocks on clicked layer
                            // Edge block = has at least one empty neighbor
                            std::vector<int> toRemove;
                            for (int bi = 0; bi < (int)room.Blocks.size(); ++bi)
                            {
                                auto& b = room.Blocks[bi];
                                if (b.Y != clickY) continue;
                                bool edge = false;
                                int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                                for (auto& d : dirs)
                                {
                                    bool hasNeighbor = false;
                                    for (auto& nb : room.Blocks)
                                        if (nb.X == b.X+d[0] && nb.Z == b.Z+d[1] && nb.Y == clickY)
                                            { hasNeighbor = true; break; }
                                    if (!hasNeighbor) { edge = true; break; }
                                }
                                if (edge) toRemove.push_back(bi);
                            }
                            // Keep at least 1 block
                            if ((int)toRemove.size() < (int)room.Blocks.size())
                            {
                                PushRoomEditState();
                                for (int ri = (int)toRemove.size() - 1; ri >= 0; --ri)
                                {
                                    int idx = toRemove[ri];
                                    int bx = room.Blocks[idx].X, bz = room.Blocks[idx].Z, by = room.Blocks[idx].Y;
                                    room.Blocks.erase(room.Blocks.begin() + idx);
                                    for (int di = (int)room.Doors.size()-1; di >= 0; --di)
                                        if (room.Doors[di].BlockX == bx && room.Doors[di].BlockZ == bz && room.Doors[di].BlockY == by)
                                            room.Doors.erase(room.Doors.begin() + di);
                                }
                            }
                        }
                        else if (ctrlHeld && !shiftHeld)
                        {
                            // Ctrl = extend: add a new layer of blocks around existing edges
                            std::vector<RoomBlock> newBlocks;
                            for (auto& b : room.Blocks)
                            {
                                if (b.Y != clickY) continue;
                                int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
                                for (auto& d : dirs)
                                {
                                    int nx = b.X + d[0], nz = b.Z + d[1];
                                    bool exists = false;
                                    for (auto& eb : room.Blocks)
                                        if (eb.X == nx && eb.Z == nz && eb.Y == clickY)
                                            { exists = true; break; }
                                    if (exists) continue;
                                    // Check not already in newBlocks
                                    bool dup = false;
                                    for (auto& nb : newBlocks)
                                        if (nb.X == nx && nb.Z == nz && nb.Y == clickY)
                                            { dup = true; break; }
                                    if (!dup) { RoomBlock rb; rb.X = nx; rb.Y = clickY; rb.Z = nz; newBlocks.push_back(rb); }
                                }
                            }
                            if (!newBlocks.empty())
                            {
                                PushRoomEditState();
                                for (auto& nb : newBlocks)
                                    room.Blocks.push_back(nb);
                            }
                        }
                        else if (shiftHeld && existIdx >= 0)
                        {
                            // Shift+click = remove block
                            if ((int)room.Blocks.size() > 1)
                            {
                                PushRoomEditState();
                                room.Blocks.erase(room.Blocks.begin() + existIdx);
                                for (int di = (int)room.Doors.size() - 1; di >= 0; --di)
                                    if (room.Doors[di].BlockX == gx && room.Doors[di].BlockZ == gz &&
                                        room.Doors[di].BlockY == clickY)
                                        room.Doors.erase(room.Doors.begin() + di);
                            }
                        }
                        else if (!shiftHeld && !ctrlHeld && existIdx >= 0)
                        {
                            // Plain click on existing block = select it
                            m_UIManager.SetMazeSelectedBlock(existIdx);
                        }
                        else if (!shiftHeld && !ctrlHeld && existIdx < 0)
                        {
                            // Plain click on empty = place new block (must be adjacent or first block)
                            bool adjacent = false;
                            for (const auto& b : room.Blocks)
                            {
                                int dx = std::abs(b.X - gx);
                                int dz = std::abs(b.Z - gz);
                                int dy = std::abs(b.Y - clickY);
                                if ((dx + dz + dy) == 1)
                                { adjacent = true; break; }
                            }
                            if (adjacent || room.Blocks.empty())
                            {
                                PushRoomEditState();
                                RoomBlock nb;
                                nb.X = gx; nb.Y = clickY; nb.Z = gz;
                                room.Blocks.push_back(nb);
                                m_UIManager.SetMazeSelectedBlock(-1);
                            }
                        }
                    }
                }
            }
        }

        // ---- Lightmap bake request (async UV2 atlas baker) ----
        if (m_UIManager.HasPendingBakeLighting())
        {
            m_UIManager.ConsumePendingBakeLighting();
            StartSceneBakeAsync();
        }
        PollSceneBakeAsync();

        // ---- Clear scene lightmap request ----
        if (m_UIManager.HasPendingClearSceneLightmap())
        {
            m_UIManager.ConsumePendingClearSceneLightmap();
            Renderer::ClearGlobalLightmaps();
            if (!m_SceneLightmapPath.empty())
            {
                LightmapManager::Instance().Release(m_SceneLightmapPath);
                m_SceneLightmapPath.clear();
            }
            if (!m_SceneLightmapPathX.empty())
            {
                LightmapManager::Instance().Release(m_SceneLightmapPathX);
                m_SceneLightmapPathX.clear();
            }
            if (!m_SceneLightmapPathZ.empty())
            {
                LightmapManager::Instance().Release(m_SceneLightmapPathZ);
                m_SceneLightmapPathZ.clear();
            }
            m_UIManager.SetSceneLightmapPath("");
            for (int ei = 0; ei < (int)m_Scene.MeshRenderers.size(); ++ei)
            {
                auto& mr = m_Scene.MeshRenderers[ei];
                mr.LightmapID  = 0;
                mr.LightmapIndex = -1;
                mr.LightmapIDX = 0;
                mr.LightmapIDY = 0;
                mr.LightmapIDZ = 0;
                mr.LightmapST  = {1,1,0,0};
                mr.LightmapSTX = {1,1,0,0};
                mr.LightmapSTY = {1,1,0,0};
                mr.LightmapSTZ = {1,1,0,0};
            }
            m_UIManager.SetBakeProgress(false, 0.0f, "");
        }

        // Compute viewport drop position for drag-to-viewport
        {
            double mxd, myd;
            InputManager::GetMousePosition(mxd, myd);
            m_UIManager.SetViewportDropPos(ComputeViewportSpawnPoint((float)mxd, (float)myd, winW, winH));
        }

        // Render 3D scene
        if (showPrefabEditor)
        {
            // Render the prefab preview scene
            Renderer::RenderSceneEditor(m_PrefabPreviewScene, m_EditorCamera, winW, winH);

            // Draw gizmo on selected prefab node
            int selNode = m_UIManager.GetPrefabSelectedNode();
            int selEnt  = (selNode >= 0) ? selNode + 1 : -1;
            if (selEnt >= 0 && selEnt < (int)m_PrefabPreviewScene.Transforms.size())
            {
                int axisInt = (int)m_Gizmo.GetDragAxis();
                glm::vec3 gizmoPos = m_PrefabPreviewScene.Transforms[selEnt].Position;
                if (m_GizmoMode == GizmoMode::Move)
                    Renderer::DrawTranslationGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Rotate)
                    Renderer::DrawRotationGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Scale)
                    Renderer::DrawScaleGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
            }
        }
        else if (showGameView)
        {
            Renderer::RenderSceneGame(m_Scene, winW, winH);
        }
        else if (showRoomEditor)
        {
            // Render the room preview scene (interior entities + floor)
            Renderer::RenderSceneEditor(m_RoomPreviewScene, m_RoomEditorCamera, winW, winH);

            // Draw wireframe block outlines + door markers + expansion arrows
            int rsi = m_UIManager.GetEditingRoomSetIdx();
            int rri = m_UIManager.GetEditingRoomIdx();
            if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
            {
                if (m_UIManager.GetRoomShowBlocks())
                {
                    Renderer::DrawRoomBlockWireframes(
                        m_Scene.RoomSets[rsi].Rooms[rri],
                        m_UIManager.GetMazeSelectedBlock(),
                        m_MazeHoveredBlock,
                        m_RoomEditorCamera, winW, winH);

                    if (!m_MazeBlockArrows.empty())
                        Renderer::DrawRoomExpansionArrows(
                            m_MazeBlockArrows.data(), (int)m_MazeBlockArrows.size(),
                            m_MazeHoveredArrowIdx,
                            InputManager::IsKeyPressed(Key::LeftShift),
                            m_RoomEditorCamera, winW, winH);
                }
            }

            // Draw gizmo on selected interior node
            int selNode = m_UIManager.GetRoomSelectedNode();
            int selEnt  = (selNode >= 0) ? selNode + 1 : -1; // +1 for preview light
            if (selEnt >= 0 && selEnt < (int)m_RoomPreviewScene.Transforms.size())
            {
                int axisInt = (int)m_Gizmo.GetDragAxis();
                glm::vec3 gizmoPos = m_RoomPreviewScene.Transforms[selEnt].Position;
                if (m_GizmoMode == GizmoMode::Move)
                    Renderer::DrawTranslationGizmo(gizmoPos, axisInt, m_RoomEditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Rotate)
                    Renderer::DrawRotationGizmo(gizmoPos, axisInt, m_RoomEditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Scale)
                    Renderer::DrawScaleGizmo(gizmoPos, axisInt, m_RoomEditorCamera, winW, winH);
            }
        }
        else if (!showProjectView && !showMazeOverview)
        {
            Renderer::RenderSceneEditor(m_Scene, m_EditorCamera, winW, winH);
            if (m_Mode == EngineMode::Editor && m_SelectedEntity >= 0)
            {
                int axisInt = (int)m_Gizmo.GetDragAxis();
                glm::vec3 gizmoWorldPos = m_Scene.GetEntityWorldPos(m_SelectedEntity);
                if (m_GizmoMode == GizmoMode::Move)
                    Renderer::DrawTranslationGizmo(
                        gizmoWorldPos,
                        axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Rotate)
                    Renderer::DrawRotationGizmo(
                        gizmoWorldPos,
                        axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Scale)
                    Renderer::DrawScaleGizmo(
                        gizmoWorldPos,
                        axisInt, m_EditorCamera, winW, winH);
            }
        }

        // Game view without any camera: black screen + explicit message
        if (showGameView && m_Scene.GetActiveGameCamera() < 0)
        {
            ImGuiWindowFlags ovFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
            ImGui::SetNextWindowPos(
                ImVec2((float)winW * 0.5f, (float)winH * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowBgAlpha(0.40f);
            ImGui::Begin("##nocam_overlay", nullptr, ovFlags);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "No camera exists in the scene.");
            ImGui::End();
        }

        // Feed play/pause state to UIManager (rendered as ImGui buttons)
        m_UIManager.SetPlayState(m_Mode != EngineMode::Editor, m_Mode == EngineMode::Paused);

        // UI panels — always visible in all modes
        m_UIManager.Render(m_Scene, m_SelectedEntity, winW, winH, s_DroppedFiles, m_GizmoMode);

        // Entity Network HUD overlay
        if (m_UIManager.IsEntityNetHUDEnabled())
        {
            auto projectToScreen = [&](const glm::vec3& wp) -> ImVec2 {
                int gcIdx = m_Scene.GetActiveGameCamera();
                bool useGame = showGameView && gcIdx >= 0;
                glm::mat4 view;
                glm::mat4 proj;
                float aspect = (float)winW / (float)winH;
                if (useGame) {
                    auto& cam = m_Scene.GameCameras[gcIdx];
                    auto& tr  = m_Scene.Transforms[gcIdx];
                    view = glm::lookAt(tr.Position, tr.Position + cam.Front, cam.Up);
                    proj = cam.GetProjection(aspect);
                } else {
                    view = m_EditorCamera.GetViewMatrix();
                    proj = m_EditorCamera.GetProjection(aspect);
                }
                glm::vec4 clip = proj * view * glm::vec4(wp, 1.0f);
                if (clip.w <= 0.0001f) return ImVec2(-10000.0f, -10000.0f);
                glm::vec3 ndc = glm::vec3(clip) / clip.w;
                float sx = (ndc.x * 0.5f + 0.5f) * (float)winW;
                float sy = (-ndc.y * 0.5f + 0.5f) * (float)winH;
                return ImVec2(sx, sy);
            };
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            for (int i = 0; i < (int)m_Scene.MultiplayerControllers.size(); ++i)
            {
                const auto& mc = m_Scene.MultiplayerControllers[i];
                if (!mc.Active || !mc.Enabled) continue;
                glm::vec3 wp = m_Scene.GetEntityWorldPos(i);
                ImVec2 sp = projectToScreen(wp + glm::vec3(0, 1.2f, 0));
                if (sp.x < -1000.0f) continue;
                char buf[128];
                snprintf(buf, sizeof(buf), "%s  id:%llu  local:%s  repl:%s",
                         mc.Nickname.c_str(), (unsigned long long)mc.NetworkId,
                         mc.OwnedLocally ? "Y" : "N",
                         mc.Replicated ? "Y" : "N");
                ImU32 col = mc.OwnedLocally ? IM_COL32(240,220,70,255) : IM_COL32(150,200,255,255);
                dl->AddText(ImVec2(sp.x, sp.y), col, buf);
            }
        }

        // Viewport right-click context menu (editor mode only)
        if (m_Mode == EngineMode::Editor && !showGameView && !showProjectView && m_OpenViewportMenu)
        {
            if (ImGui::BeginPopup("##vp_ctx"))
            {
                const bool hasHitEntity =
                    m_RmbHitEntity >= 0 && m_RmbHitEntity < (int)m_Scene.EntityNames.size();

                if (hasHitEntity)
                {
                    ImGui::TextDisabled("Target: %s", m_Scene.EntityNames[m_RmbHitEntity].c_str());
                    if (ImGui::MenuItem("Select Target"))
                        m_SelectedEntity = m_RmbHitEntity;
                    if (ImGui::MenuItem("Delete Target"))
                    {
                        if (m_SelectedEntity == m_RmbHitEntity)      m_SelectedEntity = -1;
                        else if (m_SelectedEntity > m_RmbHitEntity)  m_SelectedEntity--;
                        m_Scene.DeleteEntity(m_RmbHitEntity);
                        m_RmbHitEntity = -1;
                    }
                    ImGui::Separator();
                }

                if (ImGui::BeginMenu("Create 3D Object"))
                {
                    int parent = hasHitEntity ? m_RmbHitEntity : -1;
                    if (ImGui::MenuItem("Cube"))     CreateViewportEntity("Cube",     parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Sphere"))   CreateViewportEntity("Sphere",   parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Plane"))    CreateViewportEntity("Plane",    parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Pyramid"))  CreateViewportEntity("Pyramid",  parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Cylinder")) CreateViewportEntity("Cylinder", parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Capsule"))  CreateViewportEntity("Capsule",  parent, m_RmbWorldPos);
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Create Lights"))
                {
                    int parent = hasHitEntity ? m_RmbHitEntity : -1;
                    if (ImGui::MenuItem("Directional Light")) CreateViewportLight(LightType::Directional, parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Point Light"))       CreateViewportLight(LightType::Point,       parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Spot Light"))        CreateViewportLight(LightType::Spot,        parent, m_RmbWorldPos);
                    if (ImGui::MenuItem("Area Light"))        CreateViewportLight(LightType::Area,        parent, m_RmbWorldPos);
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Create Render"))
                {
                    int parent = hasHitEntity ? m_RmbHitEntity : -1;
                    if (ImGui::MenuItem("Camera")) CreateViewportEntity("Camera", parent, m_RmbWorldPos);
                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem("Empty Object"))
                {
                    int parent = hasHitEntity ? m_RmbHitEntity : -1;
                    CreateViewportEntity("Empty", parent, m_RmbWorldPos);
                }

                if (ImGui::MenuItem("Multiplayer Manager"))
                {
                    Entity e = m_Scene.CreateEntity("Multiplayer Manager");
                    int id = (int)e.GetID();
                    m_Scene.Transforms[id].Position = m_RmbWorldPos;
                    if (hasHitEntity)
                        m_Scene.SetEntityParent(id, m_RmbHitEntity);
                    m_SelectedEntity = id;
                    if (id < (int)m_Scene.MultiplayerManagers.size())
                    {
                        m_Scene.MultiplayerManagers[id].Active = true;
                        m_Scene.MultiplayerManagers[id].Enabled = true;
                    }
                }
                ImGui::EndPopup();
            }
            else
            {
                m_OpenViewportMenu = false;
            }
        }

        // Scroll-speed HUD – show for 2 s after Shift+Scroll changes speed (scene view only)
        if (!showGameView && !showProjectView && m_EditorCamera.GetScrollSpeedDisplayTimer() > 0.0f)
        {
            ImGuiWindowFlags ovFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
            ImGui::SetNextWindowPos(
                ImVec2((float)winW * 0.5f, 60.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowBgAlpha(0.70f);
            ImGui::Begin("##scrollspeed_hud", nullptr, ovFlags);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                "Scroll Speed: %.2f", m_EditorCamera.GetScrollSpeed());
            ImGui::End();
        }
        m_UIManager.EndFrame();

        m_Window.Update();
    }

    if (m_BakeThread.joinable())
        m_BakeThread.join();

    m_UIManager.Shutdown();
    LightmapManager::Instance().ReleaseAll();
}

} // namespace tsu
