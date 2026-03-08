#include "core/application.h"
#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "renderer/lightBaker.h"
#include "renderer/lightmapManager.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include "procedural/mazeGenerator.h"
#include "lua/luaSystem.h"
#include "scene/entity.h"
#include "serialization/sceneSerializer.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
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

Application::Application()
    : m_Window(1280, 720, "TSU Engine  |  ALPHA 1.1.0 - NOT RELEASE VERSION, EXPECT BUGS")
{
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

    // Clear maze generator runtime state (spawned rooms + occupancy)
    for (auto& gen : m_Scene.MazeGenerators)
    {
        gen.SpawnedRooms.clear();
        gen.OccupiedCells.clear();
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

        m_ProjectName   = name;
        m_ProjectFolder = fs::absolute(dir).string();
        m_UIManager.SetProjectDisplayName(m_ProjectName);
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
                if (k == "name")  projName  = v;
                if (k == "scene") scenePath = projFolder + "/" + v;
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

        m_ProjectName    = projName;
        m_ProjectFolder  = fs::absolute(projFolder).string();

        // Free existing mesh pointers before clearing the scene
        for (auto& mr : m_Scene.MeshRenderers)
            if (mr.MeshPtr) { delete mr.MeshPtr; mr.MeshPtr = nullptr; }

        m_Scene          = Scene{};
        m_SelectedEntity = -1;

        if (fs::exists(scenePath))
            SceneSerializer::Load(m_Scene, scenePath);
        else
            m_UIManager.Log("[Project] Warning: scene not found: " + scenePath);

        m_UIManager.SetProjectDisplayName(m_ProjectName);
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
            SceneSerializer::Save(m_Scene, "assets/scenes/default.tscene");
            m_UIManager.Log("[Project] Saved scene to assets/scenes/default.tscene");
            return;
        }

        fs::create_directories(m_ProjectFolder + "/assets/scenes");
        fs::create_directories(m_ProjectFolder + "/assets/scripts");

        // Save the main scene
        SceneSerializer::Save(m_Scene, m_ProjectFolder + "/assets/scenes/main.tscene");

        // Copy assets directory (scripts, textures, meshes, prefabs…) if it exists
        if (fs::exists("assets")) {
            fs::copy("assets", m_ProjectFolder + "/assets",
                     fs::copy_options::recursive | fs::copy_options::update_existing);
        }

        // Write/refresh .tsproj
        {
            std::ofstream f(m_ProjectFolder + "/" + m_ProjectName + ".tsproj");
            f << "# tsuEngine Project File\n"
              << "name  = " << m_ProjectName << "\n"
              << "scene = assets/scenes/main.tscene\n";
        }

        m_UIManager.Log("[Project] Project saved: " + m_ProjectFolder);
    } catch (const std::exception& ex) {
        m_UIManager.Log(std::string("[Project] Error saving: ") + ex.what());
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
            std::ofstream marker(outputFolder + "/game.mode");
            marker << "# tsuEngine standalone game marker\n";
            marker << "title=" << gameName << "\n";
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
    }

    m_RoomPreviewSetIdx  = rsi;
    m_RoomPreviewRoomIdx = rri;
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
            if (!m_PrefabPreviewScene.MeshRenderers[ei].MeshPtr) continue;
            glm::vec3 pos  = m_PrefabPreviewScene.GetEntityWorldPos(ei);
            glm::vec3 sc   = m_PrefabPreviewScene.Transforms[ei].Scale;
            float radius   = std::max({sc.x, sc.y, sc.z}) * 0.5f;
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
                glm::vec3 move = m_Gizmo.OnMouseDrag(m_EditorCamera,
                                                       delta.x, delta.y,
                                                       winW, winH);
                m_RoomPreviewScene.Transforms[selEntity].Position += move;
            }
            else if (m_GizmoMode == GizmoMode::Rotate)
            {
                float deg = m_Gizmo.OnMouseDragRotation(m_EditorCamera,
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
                glm::vec3 sd = m_Gizmo.OnMouseDragScale(m_EditorCamera,
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
    glm::mat4 vp  = m_EditorCamera.GetProjection(aspect) * m_EditorCamera.GetViewMatrix();
    glm::mat4 vpI = glm::inverse(vp);
    float ndcX =  2.0f * mx / (float)winW - 1.0f;
    float ndcY = -2.0f * my / (float)winH + 1.0f;
    glm::vec4 nearH = vpI * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farH  = vpI * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
    glm::vec3 rayOrig = glm::vec3(nearH) / nearH.w;
    glm::vec3 rayDir  = glm::normalize(glm::vec3(farH) / farH.w - rayOrig);

    // ---- Compute per-block exposed face arrows ----
    m_MazeBlockArrows.clear();
    m_MazeHoveredArrowIdx = -1;
    m_MazeHoveredBlock    = -1;
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

                if (shiftHeld)
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
                m_EditorCamera, mx, my, winW, winH, gizmoHitMode);
            if (axis != EditorGizmo::Axis::None)
                return;
        }

        // === Raycast preview scene for interior node selection ===
        float bestT = 1e30f;
        int hitNode = -1;
        for (int ei = 1; ei < (int)m_RoomPreviewScene.Transforms.size(); ++ei)
        {
            if (!m_RoomPreviewScene.MeshRenderers[ei].MeshPtr) continue;
            glm::vec3 pos  = m_RoomPreviewScene.GetEntityWorldPos(ei);
            glm::vec3 sc   = m_RoomPreviewScene.Transforms[ei].Scale;
            float radius   = std::max({sc.x, sc.y, sc.z}) * 0.5f;
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
                m_Scene.Transforms[camEnt].Position.x += swayX * 0.25f; // subtle lateral
            }
            else
            {
                // Smoothly return to rest position
                float restY = pc._HeadbobBaseY;
                float curY  = m_Scene.Transforms[camEnt].Position.y;
                m_Scene.Transforms[camEnt].Position.y = curY + (restY - curY) * std::min(dt / 0.08f, 1.0f);
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
            }
        }
        else if (wasInside && !anyPlayerInside)
        {
            // Exit event
            if (tr.Channel >= 0)
                m_Scene.SetChannelBool(tr.Channel, tr.OutsideValue);
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
            an._Timer += dt;
            if (an._Timer < an.Delay) continue;
            an._Timer = 0.0f;
            an._DelayDone = true;
        }

        float dur = std::max(an.Duration, 0.001f);
        an._Timer += dt;
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
        glm::vec3 value = glm::mix(an.From, an.To, easedT);

        // Apply to entity transform
        if (i < (int)m_Scene.Transforms.size())
        {
            auto& tr = m_Scene.Transforms[i];
            if      (an.Property == AnimatorProperty::Position) tr.Position = value;
            else if (an.Property == AnimatorProperty::Rotation) tr.Rotation = value;
            else if (an.Property == AnimatorProperty::Scale)    tr.Scale    = value;
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

void Application::HandleToolbarInput()
{
    // Mouse click on the toolbar
    if (InputManager::IsMouseDown(Mouse::Left))
    {
        double mx, my;
        InputManager::GetMousePosition(mx, my);
        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        if (Renderer::ToolbarClickPlay((int)mx,(int)my, winW, winH, (float)UIManager::k_MenuBarH))
        {
            if (m_Mode == EngineMode::Editor)
            {
                SaveEditorState();
                m_Mode = EngineMode::Game;
                LuaSystem::Init(m_Scene);
                LuaSystem::StartScripts();
            }
            else
            {
                LuaSystem::Shutdown();
                RestoreEditorState();
                m_Mode = EngineMode::Editor;
            }
        }

        if (Renderer::ToolbarClickPause((int)mx,(int)my, winW, winH, (float)UIManager::k_MenuBarH))
        {
            if (m_Mode == EngineMode::Game)   m_Mode = EngineMode::Paused;
            else if (m_Mode == EngineMode::Paused) m_Mode = EngineMode::Game;
        }
    }

    // Atalho de teclado: F5 = play/stop, F6 = pause
    if (InputManager::IsKeyDown(Key::F1))
    {
        if (m_Mode == EngineMode::Editor)
        {
            SaveEditorState();
            m_Mode = EngineMode::Game;
            LuaSystem::Init(m_Scene);
            LuaSystem::StartScripts();
        }
        else
        {
            LuaSystem::Shutdown();
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
            UpdateTriggers(deltaTime);
            UpdateAnimators(deltaTime);
            LuaSystem::UpdateScripts(m_Scene, deltaTime);
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

        // s_DroppedFiles is processed in UIManager::Render (which knows the current folder)
        Renderer::BeginFrame();

        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        // Game logic (only when actually running)
        if (m_Mode == EngineMode::Game)
        {
            UpdatePlayerControllers(deltaTime);
            UpdateMouseLook(deltaTime);
            UpdateTriggers(deltaTime);
            UpdateAnimators(deltaTime);
            LuaSystem::UpdateScripts(m_Scene, deltaTime);
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

        // Editor camera update: needed when showing scene view, prefab editor, or room editor
        if (!showGameView && !showProjectView && !showMazeOverview)
            m_EditorCamera.OnUpdate(deltaTime);

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
                }
            }
        }
        else
        {
            // If we just left the prefab editor, drop the preview scene
            if (m_PrefabPreviewIdx >= 0)
            {
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
            // Block cubes may have changed — rebuild each frame (cheap for small rooms)
            else
                RebuildRoomPreview();
        }
        else
        {
            if (m_RoomPreviewSetIdx >= 0)
            {
                m_RoomPreviewScene = Scene();
                m_RoomPreviewSetIdx = -1;
                m_RoomPreviewRoomIdx = -1;
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
            float vpW = (float)(winW - m_UIManager.GetPanelWidth() * 2);
            float vpH = (float)(winH - UIManager::k_AssetH - UIManager::k_TopStackH);
            float aspect = vpW / std::max(vpH, 1.0f);
            glm::mat4 proj = m_EditorCamera.GetProjection(aspect);
            glm::mat4 view = m_EditorCamera.GetViewMatrix();
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

        // ---- Lightmap bake request (raised by "Bake Lighting" toolbar button) ----
        if (m_UIManager.HasPendingBakeLighting())
        {
            m_UIManager.ConsumePendingBakeLighting();
            int rsi = m_UIManager.GetEditingRoomSetIdx();
            int ri  = m_UIManager.GetEditingRoomIdx();
            if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                ri  >= 0 && ri  < (int)m_Scene.RoomSets[rsi].Rooms.size())
            {
                RoomTemplate& room = m_Scene.RoomSets[rsi].Rooms[ri];
                auto lights = LightBaker::ExtractLights(m_Scene);
                LightBaker::BakeResult result = LightBaker::BakeRoom(room, lights);

                // Build output path: assets/lightmaps/<roomName>.tga
                std::string outDir  = "assets/lightmaps";
                std::filesystem::create_directories(outDir);
                std::string outPath = outDir + "/" + room.Name + ".tga";
                bool saved = LightmapManager::SaveTGA(
                    outPath, result.width, result.height, result.pixels.data());

                if (saved)
                {
                    room.LightmapPath = outPath;
                    room.LightmapST   = result.lightmapST;
                    m_UIManager.SetBakeStatus("Baked!");
                    m_UIManager.Log("[LightBaker] Baked: " + outPath);
                }
                else
                {
                    m_UIManager.SetBakeStatus("Error: save failed");
                    m_UIManager.Log("[LightBaker] Error: could not save to " + outPath);
                }
            }
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
            Renderer::RenderSceneEditor(m_RoomPreviewScene, m_EditorCamera, winW, winH);

            // Draw wireframe block outlines + door markers + expansion arrows
            int rsi = m_UIManager.GetEditingRoomSetIdx();
            int rri = m_UIManager.GetEditingRoomIdx();
            if (rsi >= 0 && rsi < (int)m_Scene.RoomSets.size() &&
                rri >= 0 && rri < (int)m_Scene.RoomSets[rsi].Rooms.size())
            {
                Renderer::DrawRoomBlockWireframes(
                    m_Scene.RoomSets[rsi].Rooms[rri],
                    m_UIManager.GetMazeSelectedBlock(),
                    m_MazeHoveredBlock,
                    m_EditorCamera, winW, winH);

                if (!m_MazeBlockArrows.empty())
                    Renderer::DrawRoomExpansionArrows(
                        m_MazeBlockArrows.data(), (int)m_MazeBlockArrows.size(),
                        m_MazeHoveredArrowIdx,
                        InputManager::IsKeyPressed(Key::LeftShift),
                        m_EditorCamera, winW, winH);
            }

            // Draw gizmo on selected interior node
            int selNode = m_UIManager.GetRoomSelectedNode();
            int selEnt  = (selNode >= 0) ? selNode + 1 : -1; // +1 for preview light
            if (selEnt >= 0 && selEnt < (int)m_RoomPreviewScene.Transforms.size())
            {
                int axisInt = (int)m_Gizmo.GetDragAxis();
                glm::vec3 gizmoPos = m_RoomPreviewScene.Transforms[selEnt].Position;
                if (m_GizmoMode == GizmoMode::Move)
                    Renderer::DrawTranslationGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Rotate)
                    Renderer::DrawRotationGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
                else if (m_GizmoMode == GizmoMode::Scale)
                    Renderer::DrawScaleGizmo(gizmoPos, axisInt, m_EditorCamera, winW, winH);
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

        // Toolbar always visible on top
        Renderer::RenderToolbar(
            m_Mode != EngineMode::Editor,
            m_Mode == EngineMode::Paused,
            winW, winH,
            (float)UIManager::k_MenuBarH);

        // UI panels — always visible in all modes
        m_UIManager.Render(m_Scene, m_SelectedEntity, winW, winH, s_DroppedFiles, m_GizmoMode);

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

    m_UIManager.Shutdown();
    LightmapManager::Instance().ReleaseAll();
}

} // namespace tsu