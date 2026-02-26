#include "core/application.h"
#include "renderer/renderer.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
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
    // Não interfere quando o mouse direito está girando a camera
    if (InputManager::IsMousePressed(Mouse::Right)) return;

    // Não interfere quando o ImGui está capturando o mouse (clique em painel)
    if (m_UIManager.WantCaptureMouse()) return;

    double mxd, myd;
    InputManager::GetMousePosition(mxd, myd);
    float mx = (float)mxd;
    float my = (float)myd;

    // Ignora cliques na faixa da toolbar (topo ~7% da janela)
    float nyNorm = 1.0f - 2.0f * my / (float)winH;
    if (nyNorm > 0.93f) return;

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

        if (Renderer::ToolbarClickPlay((int)mx,(int)my, winW, winH))
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

        if (Renderer::ToolbarClickPause((int)mx,(int)my, winW, winH))
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
        HandleToolbarInput();

        // s_DroppedFiles é processado em UIManager::Render (que conhece a pasta atual)
        m_UIManager.BeginFrame();
        Renderer::BeginFrame();

        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        if (m_Mode == EngineMode::Editor)
        {
            m_EditorCamera.OnUpdate(deltaTime);
            m_Scene.OnUpdate(deltaTime);
            HandleEditorInput(winW, winH);
            Renderer::RenderSceneEditor(m_Scene, m_EditorCamera, winW, winH);

            // Desenha gizmo sobre tudo
            if (m_SelectedEntity >= 0)
            {
                Renderer::DrawTranslationGizmo(
                    m_Scene.Transforms[m_SelectedEntity].Position,
                    (int)m_Gizmo.GetDragAxis(),
                    m_EditorCamera, winW, winH);
            }
        }
        else if (m_Mode == EngineMode::Game)
        {
            m_Scene.OnUpdate(deltaTime);
            PhysicsSystem::Update(m_Scene, deltaTime);
            Renderer::RenderSceneGame(m_Scene, winW, winH);
        }
        else // Paused
        {
            Renderer::RenderSceneGame(m_Scene, winW, winH);
        }

        // Toolbar sempre visível por cima
        Renderer::RenderToolbar(
            m_Mode != EngineMode::Editor,
            m_Mode == EngineMode::Paused,
            winW, winH);

        // UI panels (only in editor mode)
        if (m_Mode == EngineMode::Editor)
        {
            m_UIManager.Render(m_Scene, m_SelectedEntity, winW, winH, s_DroppedFiles);

            // Scroll-speed HUD – show for 2 s after Shift+Scroll changes speed
            if (m_EditorCamera.GetScrollSpeedDisplayTimer() > 0.0f)
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
        }
        m_UIManager.EndFrame();

        m_Window.Update();
    }

    m_UIManager.Shutdown();
}

} // namespace tsu