#include "core/application.h"
#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include "scene/entity.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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
    : m_Window(1280, 720, "tsuEngine")
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
    for (size_t i=0; i<m_Snapshot.size() && i<m_Scene.Transforms.size(); i++)
    {
        m_Scene.Transforms[i].Position          = m_Snapshot[i].position;
        m_Scene.RigidBodies[i].Velocity         = m_Snapshot[i].velocity;
        m_Scene.Transforms[i].Rotation          = m_Snapshot[i].rotation;
        m_Scene.RigidBodies[i].AngularVelocity  = m_Snapshot[i].angularVelocity;
        m_Scene.RigidBodies[i].IsGrounded       = false;
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
            // Get world-space forward vector (local +X projected onto the XZ plane).
            glm::mat4 worldMat = m_Scene.GetEntityWorldMatrix(i);
            glm::vec3 fwd = glm::vec3(worldMat * glm::vec4(1,0,0,0));
            fwd.y = 0.0f;
            if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
            // Right vector is cross(forward, world-up) = left-perpendicular in XZ plane.
            glm::vec3 right = glm::cross(fwd, glm::vec3(0,1,0));
            worldMove = (fwd * axis.x + right * axis.z) * speed * dt;
        }
        else
        {
            // MODE 1 / 3 — World space: forward = world +X, right = world +Z.
            // Player rotation has NO effect on the movement direction.
            worldMove = glm::vec3(axis.x, 0.0f, axis.z) * speed * dt;
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
            }
            else
            {
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
        if (m_Mode == EngineMode::Editor) { SaveEditorState(); m_Mode = EngineMode::Game; }
        else { RestoreEditorState(); m_Mode = EngineMode::Editor; }
    }
    if (InputManager::IsKeyDown(Key::F2))
    {
        if (m_Mode == EngineMode::Game)   m_Mode = EngineMode::Paused;
        else if (m_Mode == EngineMode::Paused) m_Mode = EngineMode::Game;
    }
}

void Application::Run()
{
    Renderer::Init();
    m_UIManager.Init(m_Window.GetNativeWindow());

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

        // s_DroppedFiles is processed in UIManager::Render (which knows the current folder)
        Renderer::BeginFrame();

        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        // Game logic (only when actually running)
        if (m_Mode == EngineMode::Game)
        {
            UpdatePlayerControllers(deltaTime);
            UpdateMouseLook(deltaTime);
            m_Scene.OnUpdate(deltaTime);
            PhysicsSystem::Update(m_Scene, deltaTime);
        }
        else
        {
            m_Scene.OnUpdate(deltaTime);
        }

        // Determine which view to render in the viewport
        bool showGameView      = (m_UIManager.GetViewportTab() == 1);
        bool showProjectView   = (m_UIManager.GetViewportTab() == 2);
        bool showPrefabEditor  = m_UIManager.IsPrefabEditorActive();

        // Editor camera update: needed when showing scene view or prefab editor
        if (!showGameView && !showProjectView)
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

        // Editor picking/gizmo (only in editor mode + scene view)
        if (m_Mode == EngineMode::Editor && !showGameView && !showProjectView && !showPrefabEditor)
            HandleEditorInput(winW, winH);

        // Prefab editor gizmo
        if (showPrefabEditor)
            HandlePrefabEditorInput(winW, winH);

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
        else if (!showProjectView)
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
}

} // namespace tsu