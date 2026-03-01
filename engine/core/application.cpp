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

// Buffer de arquivos arrastados de fora do editor (GLFW drop callback)
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

    // Não interfere quando o mouse direito está girando a camera
    if (InputManager::IsMousePressed(Mouse::Right)) return;

    // Não interfere quando o ImGui está capturando o mouse (clique em painel)
    if (m_UIManager.WantCaptureMouse()) return;

    // Ignore clicks on stacked top UI (menu + toolbar + camera tabs)
    if (my <= (float)UIManager::k_TopStackH) return;

    const MouseDelta delta = InputManager::GetMouseDelta();

    // ---- Mouse pressionado (hold) ----
    if (InputManager::IsMousePressed(Mouse::Left))
    {
        if (m_Gizmo.IsDragging() && m_SelectedEntity >= 0)
        {
            glm::vec3 move = m_Gizmo.OnMouseDrag(m_EditorCamera,
                                                   delta.x, delta.y,
                                                   winW, winH);
            m_Scene.Transforms[m_SelectedEntity].Position += move;
            return;
        }
    }

    // ---- Mouse desceu (click) ----
    if (InputManager::IsMouseDown(Mouse::Left))
    {
        // Tenta primeiro acertar um eixo do gizmo
        if (m_SelectedEntity >= 0)
        {
            auto axis = m_Gizmo.OnMouseDown(
                m_Scene.Transforms[m_SelectedEntity].Position,
                m_EditorCamera, mx, my, winW, winH);

            if (axis != EditorGizmo::Axis::None)
                return; // começou drag no gizmo
        }

        // Raycast para seleção de entidade
        m_SelectedEntity = RaycastScene(mx, my, winW, winH);
        return;
    }

    // ---- Mouse solto ----
    if (InputManager::IsMouseUp(Mouse::Left))
    {
        m_Gizmo.OnMouseUp();
    }
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

        glm::vec3 pos  = m_Scene.Transforms[i].Position;
        glm::vec3 half = hasCamera ? glm::vec3(0.3f) : m_Scene.Transforms[i].Scale * 0.5f;
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
    // Clique do mouse na toolbar
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

    // Registrar callback de drag & drop de arquivos externos (texturas)
    glfwSetDropCallback(m_Window.GetNativeWindow(), OnGLFWDrop);

    while (!m_Window.ShouldClose())
    {
        float time      = (float)glfwGetTime();
        float deltaTime = time - m_LastFrameTime;
        m_LastFrameTime = time;
        if (deltaTime > 0.05f) deltaTime = 0.05f; // cap em 20fps

        InputManager::Update(m_Window.GetNativeWindow());
        m_UIManager.BeginFrame();
        HandleToolbarInput();

        // s_DroppedFiles é processado em UIManager::Render (que conhece a pasta atual)
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
        bool showGameView    = (m_UIManager.GetViewportTab() == 1);
        bool showProjectView = (m_UIManager.GetViewportTab() == 2);

        // Editor camera update: needed when showing scene view
        if (!showGameView && !showProjectView)
            m_EditorCamera.OnUpdate(deltaTime);

        // Editor picking/gizmo (only in editor mode + scene view)
        if (m_Mode == EngineMode::Editor && !showGameView && !showProjectView)
            HandleEditorInput(winW, winH);

        // Render 3D scene
        if (showGameView)
        {
            Renderer::RenderSceneGame(m_Scene, winW, winH);
        }
        else if (!showProjectView)
        {
            Renderer::RenderSceneEditor(m_Scene, m_EditorCamera, winW, winH);
            if (m_Mode == EngineMode::Editor && m_SelectedEntity >= 0)
            {
                Renderer::DrawTranslationGizmo(
                    m_Scene.Transforms[m_SelectedEntity].Position,
                    (int)m_Gizmo.GetDragAxis(),
                    m_EditorCamera, winW, winH);
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

        // Toolbar sempre visível por cima
        Renderer::RenderToolbar(
            m_Mode != EngineMode::Editor,
            m_Mode == EngineMode::Paused,
            winW, winH,
            (float)UIManager::k_MenuBarH);

        // UI panels — always visible in all modes
        m_UIManager.Render(m_Scene, m_SelectedEntity, winW, winH, s_DroppedFiles);

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