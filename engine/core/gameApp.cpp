// ================================================================
// GameApplication — lean standalone runtime (no editor / ImGui)
// ================================================================
#include "core/gameApp.h"
#include "renderer/renderer.h"
#include "renderer/lightmapManager.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include "procedural/mazeGenerator.h"
#include "lua/luaSystem.h"
#include "serialization/sceneSerializer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tsu {

GameApplication::GameApplication()
    : m_Window(1280, 720, "Game")
{
}

// ================================================================
// Run — main game loop
// ================================================================
void GameApplication::Run()
{
    Renderer::Init();

    // Read title from game.mode marker (written by the editor's ExportGame)
    if (std::filesystem::exists("game.mode"))
    {
        std::ifstream mk("game.mode");
        std::string line;
        while (std::getline(mk, line))
        {
            if (line.rfind("title=", 0) == 0)
            {
                m_Title = line.substr(6);
                // Trim trailing whitespace
                while (!m_Title.empty() && (m_Title.back() == '\r' || m_Title.back() == ' '))
                    m_Title.pop_back();
            }
        }
    }

    glfwSetWindowTitle(m_Window.GetNativeWindow(), m_Title.c_str());

    // Borderless-fullscreen on the primary monitor
    GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(m_Window.GetNativeWindow(), monitor, 0, 0,
                         vidmode->width, vidmode->height, vidmode->refreshRate);

    // Load game scene
    if (std::filesystem::exists("assets/scenes/default.tscene"))
        SceneSerializer::Load(m_Scene, "assets/scenes/default.tscene");

    // Capture mouse cursor for first-person controls, etc.
    glfwSetInputMode(m_Window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Start Lua scripting
    LuaSystem::Init(m_Scene);
    LuaSystem::StartScripts();

    m_LastFrameTime = (float)glfwGetTime();

    // ---- Game loop ----
    while (!m_Window.ShouldClose())
    {
        float time      = (float)glfwGetTime();
        float deltaTime = time - m_LastFrameTime;
        m_LastFrameTime = time;
        if (deltaTime > 0.05f) deltaTime = 0.05f; // cap at 20 fps minimum

        InputManager::Update(m_Window.GetNativeWindow());

        // Escape quits the game
        if (InputManager::IsKeyDown(Key::Escape))
            glfwSetWindowShouldClose(m_Window.GetNativeWindow(), GLFW_TRUE);

        // ---- Game logic ----
        UpdatePlayerControllers(deltaTime);
        UpdateMouseLook(deltaTime);
        UpdateTriggers(deltaTime);
        UpdateAnimators(deltaTime);
        LuaSystem::UpdateScripts(m_Scene, deltaTime);
        m_Scene.OnUpdate(deltaTime);
        PhysicsSystem::Update(m_Scene, deltaTime);
        MazeGenerator::Update(m_Scene, deltaTime);

        // ---- Rendering ----
        int winW = m_Window.GetWidth();
        int winH = m_Window.GetHeight();

        Renderer::BeginFrame();
        Renderer::RenderSceneGame(m_Scene, winW, winH);

        m_Window.Update(); // swap buffers + poll events
    }

    // Cleanup
    LuaSystem::Shutdown();
    LightmapManager::Instance().ReleaseAll();
}

// ================================================================
// UpdatePlayerControllers
// ================================================================
void GameApplication::UpdatePlayerControllers(float dt)
{
    for (int i = 0; i < (int)m_Scene.PlayerControllers.size(); ++i)
    {
        auto& pc = m_Scene.PlayerControllers[i];
        if (!pc.Active) continue;

        bool inForward = false, inBack  = false;
        bool inLeft    = false, inRight = false;
        bool inRun     = false, inCrouch= false;

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

        const bool isLocalSpace = (pc.MovementMode == PlayerMovementMode::LocalWithCollision ||
                                   pc.MovementMode == PlayerMovementMode::LocalNoCollision);
        const bool noclip       = (pc.MovementMode == PlayerMovementMode::WorldNoCollision ||
                                   pc.MovementMode == PlayerMovementMode::LocalNoCollision);

        glm::vec3 worldMove;
        if (isLocalSpace)
        {
            glm::mat4 worldMat = m_Scene.GetEntityWorldMatrix(i);
            glm::vec3 fwd = glm::vec3(worldMat * glm::vec4(0,0,-1,0));
            fwd.y = 0.0f;
            if (glm::length(fwd) > 0.001f) fwd = glm::normalize(fwd);
            glm::vec3 right = glm::cross(fwd, glm::vec3(0,1,0));
            worldMove = (fwd * axis.x + right * axis.z) * speed * dt;
        }
        else
        {
            worldMove = glm::vec3(axis.z, 0.0f, -axis.x) * speed * dt;
        }

        m_Scene.Transforms[i].Position += worldMove;

        if (noclip)
            m_Scene.RigidBodies[i].Velocity = glm::vec3(0.0f);

        pc.IsRunning    = running;
        pc.IsCrouching  = crouching;
        pc.LastMoveAxis = axis;

        // Headbob
        if (pc.HeadbobEnabled && pc.HeadbobCameraEntity >= 0 &&
            pc.HeadbobCameraEntity < (int)m_Scene.Transforms.size())
        {
            int camEnt = pc.HeadbobCameraEntity;
            if (!pc._HeadbobBaseSet)
            {
                pc._HeadbobBaseY   = m_Scene.Transforms[camEnt].Position.y;
                pc._HeadbobBaseSet = true;
            }

            bool  isMovingNow = (glm::length(axis) > 0.01f);
            float targetBlend = isMovingNow ? 1.0f : 0.0f;
            float blendRate   = isMovingNow ? (dt / 0.12f) : (dt / 0.20f);
            pc._HeadbobBlend += (targetBlend - pc._HeadbobBlend) * std::min(blendRate, 1.0f);

            if (pc._HeadbobBlend > 0.001f)
            {
                float distWalked  = glm::length(worldMove);
                pc._HeadbobPhase += distWalked * pc.HeadbobFrequency * (float)(2.0 * M_PI);

                float bobY  = sinf(pc._HeadbobPhase) * pc.HeadbobAmplitudeY * pc._HeadbobBlend;
                float swayX = sinf(pc._HeadbobPhase * 0.5f) * pc.HeadbobAmplitudeX * pc._HeadbobBlend;

                m_Scene.Transforms[camEnt].Position.y  = pc._HeadbobBaseY + bobY;
                m_Scene.Transforms[camEnt].Position.x += swayX * 0.25f;
            }
            else
            {
                float restY = pc._HeadbobBaseY;
                float curY  = m_Scene.Transforms[camEnt].Position.y;
                m_Scene.Transforms[camEnt].Position.y = curY + (restY - curY) * std::min(dt / 0.08f, 1.0f);
            }
        }
    }
}

// ================================================================
// UpdateMouseLook
// ================================================================
void GameApplication::UpdateMouseLook(float dt)
{
    (void)dt;
    const MouseDelta delta = InputManager::GetMouseDelta();
    if (delta.x == 0.0f && delta.y == 0.0f) return;

    for (int i = 0; i < (int)m_Scene.MouseLooks.size(); ++i)
    {
        auto& ml = m_Scene.MouseLooks[i];
        if (!ml.Active || !ml.Enabled) continue;

        float dx = delta.x * ml.SensitivityX * (ml.InvertX ? -1.0f :  1.0f);
        float dy = delta.y * ml.SensitivityY * (ml.InvertY ? -1.0f :  1.0f);

        int yawEnt = ml.YawTargetEntity;
        if (yawEnt >= 0 && yawEnt < (int)m_Scene.Transforms.size())
        {
            ml.CurrentYaw += dx;
            m_Scene.Transforms[yawEnt].Rotation.y = ml.CurrentYaw;
        }

        int pitchEnt = ml.PitchTargetEntity;
        if (pitchEnt >= 0 && pitchEnt < (int)m_Scene.Transforms.size())
        {
            ml.CurrentPitch -= dy;
            if (ml.ClampPitch)
            {
                if (ml.CurrentPitch < ml.PitchMin) ml.CurrentPitch = ml.PitchMin;
                if (ml.CurrentPitch > ml.PitchMax) ml.CurrentPitch = ml.PitchMax;
            }
            m_Scene.Transforms[pitchEnt].Rotation.x = ml.CurrentPitch;
        }
    }
}

// ================================================================
// UpdateTriggers
// ================================================================
void GameApplication::UpdateTriggers(float dt)
{
    (void)dt;
    struct PlayerInfo { glm::vec3 pos; glm::vec3 half; };
    std::vector<PlayerInfo> players;

    for (int i = 0; i < (int)m_Scene.PlayerControllers.size(); ++i)
    {
        const auto& pc = m_Scene.PlayerControllers[i];
        if (!pc.Active || !pc.Enabled) continue;

        glm::vec3 pos  = m_Scene.GetEntityWorldPos(i);
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
            glm::vec3 d = glm::abs(pl.pos - center);
            if (d.x <= tr.Size.x + pl.half.x &&
                d.y <= tr.Size.y + pl.half.y &&
                d.z <= tr.Size.z + pl.half.z)
            {
                anyPlayerInside = true;
                break;
            }
        }

        bool wasInside   = tr._PlayerInside;
        tr._PlayerInside = anyPlayerInside;

        if (!wasInside && anyPlayerInside)
        {
            if (!tr.OneShot || !tr._HasFired)
            {
                if (tr.Channel >= 0)
                    m_Scene.SetChannelBool(tr.Channel, tr.InsideValue);
                tr._HasFired = true;
            }
        }
        else if (wasInside && !anyPlayerInside)
        {
            if (tr.Channel >= 0)
                m_Scene.SetChannelBool(tr.Channel, tr.OutsideValue);
        }
    }
}

// ================================================================
// UpdateAnimators
// ================================================================
void GameApplication::UpdateAnimators(float dt)
{
    auto applyEasing = [](float t, AnimatorEasing e) -> float {
        switch (e) {
            case AnimatorEasing::EaseIn:    return t * t;
            case AnimatorEasing::EaseOut:   return 1.0f - (1.0f - t) * (1.0f - t);
            case AnimatorEasing::EaseInOut: return t < 0.5f
                                                ? 2.0f * t * t
                                                : 1.0f - (-2.0f*t+2.0f)*(-2.0f*t+2.0f)*0.5f;
            default:                        return t;
        }
    };

    for (int i = 0; i < (int)m_Scene.Animators.size(); ++i)
    {
        auto& an = m_Scene.Animators[i];
        if (!an.Active || !an.Enabled) continue;

        if (an.AutoPlay && !an.Playing && !an._DelayDone)
            an.Playing = true;

        if (!an.Playing) continue;

        if (!an._DelayDone)
        {
            an._Timer += dt;
            if (an._Timer < an.Delay) continue;
            an._Timer  = 0.0f;
            an._DelayDone = true;
        }

        float dur  = std::max(an.Duration, 0.001f);
        an._Timer += dt;
        float rawT = std::min(an._Timer / dur, 1.0f);

        float phaseT = (an.Mode == AnimatorMode::Oscillate && an._Dir < 0.0f)
                       ? 1.0f - rawT : rawT;

        float     easedT = applyEasing(phaseT, an.Easing);
        glm::vec3 value  = glm::mix(an.From, an.To, easedT);

        if (i < (int)m_Scene.Transforms.size())
        {
            auto& tr = m_Scene.Transforms[i];
            if      (an.Property == AnimatorProperty::Position) tr.Position = value;
            else if (an.Property == AnimatorProperty::Rotation) tr.Rotation = value;
            else if (an.Property == AnimatorProperty::Scale)    tr.Scale    = value;
        }

        if (an._Timer >= dur)
        {
            an._Timer = 0.0f;
            if      (an.Mode == AnimatorMode::Oscillate) an._Dir *= -1.0f;
            else if (an.Mode == AnimatorMode::OneShot)   an.Playing = false;
        }
    }
}

} // namespace tsu
