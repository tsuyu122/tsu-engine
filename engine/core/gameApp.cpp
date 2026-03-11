// ================================================================
// GameApplication — lean standalone runtime (no editor / ImGui)
// ================================================================
#include "core/gameApp.h"
#include "renderer/renderer.h"
#include "renderer/mesh.h"
#include "renderer/lightmapManager.h"
#include "input/inputManager.h"
#include "physics/physicsSystem.h"
#include "procedural/mazeGenerator.h"
#include "lua/luaSystem.h"
#include "network/multiplayerSystem.h"
#include "scene/entity.h"
#include "serialization/sceneSerializer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tsu {

GameApplication::GameApplication(const std::vector<std::string>& args)
    : m_Window(1280, 720, "Game"), m_Args(args)
{
}

void GameApplication::ApplyLaunchOverrides()
{
    auto getOpt = [&](const std::string& key, const std::string& def) -> std::string {
        std::string p = key + "=";
        for (const auto& a : m_Args)
            if (a.rfind(p, 0) == 0) return a.substr(p.size());
        return def;
    };

    std::string mode = getOpt("--mp-mode", "");
    if (mode.empty()) return;
    std::string server = getOpt("--mp-server", "127.0.0.1");
    std::string nick = getOpt("--mp-nick", "Player");
    int port = 27015;
    try { port = std::stoi(getOpt("--mp-port", "27015")); } catch (...) {}
    port = std::max(1, std::min(65535, port));

    int mmIdx = -1;
    for (int i = 0; i < (int)m_Scene.MultiplayerManagers.size(); ++i)
        if (m_Scene.MultiplayerManagers[i].Active) { mmIdx = i; break; }
    if (mmIdx < 0) mmIdx = (int)m_Scene.CreateEntity("MultiplayerManager").GetID();
    if (mmIdx >= 0 && mmIdx < (int)m_Scene.MultiplayerManagers.size())
    {
        auto& mm = m_Scene.MultiplayerManagers[mmIdx];
        mm.Active = true;
        mm.Enabled = true;
        mm.AutoStart = true;
        mm.Running = true;
        mm.Port = port;
        if (mode == "client")
        {
            mm.Mode = MultiplayerMode::Client;
            mm.ServerAddress = server;
        }
        else
        {
            mm.Mode = MultiplayerMode::Host;
            mm.BindAddress = "0.0.0.0";
        }
    }

    int mcIdx = -1;
    for (int i = 0; i < (int)m_Scene.MultiplayerControllers.size(); ++i)
        if (m_Scene.MultiplayerControllers[i].Active && m_Scene.MultiplayerControllers[i].IsLocalPlayer) { mcIdx = i; break; }

    // If a player prefab is configured, SpawnMultiplayerPlayers() will handle entity creation.
    // Don't create a standalone MC entity here — it would be an orphan.
    bool hasPrefab = false;
    for (int i = 0; i < (int)m_Scene.MultiplayerManagers.size(); ++i)
        if (m_Scene.MultiplayerManagers[i].Active && m_Scene.MultiplayerManagers[i].PlayerPrefabIdx >= 0)
            { hasPrefab = true; break; }

    if (!hasPrefab) {
        if (mcIdx < 0) mcIdx = (int)m_Scene.CreateEntity("Player").GetID();
        if (mcIdx >= 0 && mcIdx < (int)m_Scene.MultiplayerControllers.size())
        {
            auto& mc = m_Scene.MultiplayerControllers[mcIdx];
            mc.Active = true;
            mc.Enabled = true;
            mc.IsLocalPlayer = true;
            mc.Nickname = nick;
            if (mc.NetworkId == 0) mc.NetworkId = MultiplayerSystem::MakeNetworkId(mc.Nickname);
        }
    }
}

// ================================================================
// SpawnMultiplayerPlayers
// ================================================================
void GameApplication::SpawnMultiplayerPlayers()
{
    auto getArg = [&](const std::string& key, const std::string& def) -> std::string {
        std::string p = key + "=";
        for (const auto& a : m_Args)
            if (a.rfind(p, 0) == 0) return a.substr(p.size());
        return def;
    };

    for (int mmi = 0; mmi < (int)m_Scene.MultiplayerManagers.size(); ++mmi)
    {
        auto& mm = m_Scene.MultiplayerManagers[mmi];
        if (!mm.Active || !mm.Enabled) continue;
        if (mm.PlayerPrefabIdx < 0 || mm.PlayerPrefabIdx >= (int)m_Scene.Prefabs.size()) continue;

        bool isHost = (mm.Mode == MultiplayerMode::Host);
        std::string nick = getArg("--mp-nick", isHost ? "Player1" : "Player2");

        glm::vec3 spawnPos0 = mm.SpawnPoint + glm::vec3(-1.5f, 0.0f, 0.0f);
        glm::vec3 spawnPos1 = mm.SpawnPoint + glm::vec3( 1.5f, 0.0f, 0.0f);

        int id0 = m_Scene.SpawnFromPrefab(mm.PlayerPrefabIdx, spawnPos0);
        int id1 = m_Scene.SpawnFromPrefab(mm.PlayerPrefabIdx, spawnPos1);

        auto setupSlot = [&](int id, bool isLocal, const std::string& slotNick)
        {
            if (id < 0) return;

            auto& mc = m_Scene.MultiplayerControllers[id];
            mc.Active        = true;
            mc.IsLocalPlayer = isLocal;
            mc.SyncTransform = true;
            mc.Nickname      = slotNick;
            if (mc.NetworkId == 0)
                mc.NetworkId = MultiplayerSystem::MakeNetworkId(slotNick);

            if (isLocal)
            {
                auto& pc = m_Scene.PlayerControllers[id];
                pc.Active       = true;
                pc.Enabled      = true;
                pc.MovementMode = PlayerMovementMode::LocalWithCollision;
            }
        };

        setupSlot(id0, isHost,  isHost  ? nick : "Player1");
        setupSlot(id1, !isHost, !isHost ? nick : "Player2");
        break;
    }
}

// ================================================================
// RenderEngineSplash — animated logo displayed before the game loop
// Matches the editor splash (ring draws in, then eyes, then smile).
// Uses a self-contained 2-D OpenGL shader and triangle geometry.
// ================================================================
static void RenderEngineSplash(GLFWwindow* win)
{
    // --- Minimal 2-D shader (square-normalised coords, X divided by aspect) ---
    auto compileShader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };
    const char* vsrc =
        "#version 460 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "uniform float uAspect;\n"
        "void main(){gl_Position=vec4(aPos.x/uAspect,aPos.y,0.0,1.0);}\n";
    const char* fsrc =
        "#version 460 core\n"
        "uniform vec4 uColor;\n"
        "out vec4 fragColor;\n"
        "void main(){fragColor=uColor;}\n";
    GLuint vs   = compileShader(GL_VERTEX_SHADER,   vsrc);
    GLuint fs   = compileShader(GL_FRAGMENT_SHADER, fsrc);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint locAspect = glGetUniformLocation(prog, "uAspect");
    GLint locColor  = glGetUniformLocation(prog, "uColor");

    // --- Geometry constants (SVG 200x200 normalised to ±1 square-space) ---
    const float kScale  = 0.38f;          // matches editor: 38% of half-height
    const float kPI     = (float)M_PI;
    const float kHalfSW = 0.06f * kScale; // half stroke width  (12 / 200 * 0.38)
    const float kRingR  = 0.90f * kScale;
    const float kEyeR   = 0.14f * kScale;
    const float kSmileR = 0.60f * kScale;

    const int kRingSegs  = 96;
    const int kEyeSegs   = 32;
    const int kSmileSegs = 48;

    // Thick-arc helper: triangle-strip outer/inner interleaved.
    // Angles follow ImGui convention (0=+X, Y-down), Y is flipped for OpenGL Y-up.
    auto buildArc = [&](std::vector<float>& v, float cx, float cy,
                        float r, float hsw, float aMin, float aMax, int segs)
    {
        float rO = r + hsw, rI = r - hsw;
        for (int i = 0; i <= segs; ++i) {
            float a = aMin + (float)i / segs * (aMax - aMin);
            float ca = cosf(a), sa = -sinf(a); // flip Y
            v.push_back(cx + rO * ca);  v.push_back(cy + rO * sa);
            v.push_back(cx + rI * ca);  v.push_back(cy + rI * sa);
        }
    };
    // Filled-circle helper: triangle-fan (centre + N+1 perimeter vertices).
    auto buildCircle = [&](std::vector<float>& v, float cx, float cy, float r, int segs)
    {
        v.push_back(cx); v.push_back(cy);
        for (int i = 0; i <= segs; ++i) {
            float a = (float)i / segs * 2.0f * kPI;
            v.push_back(cx + r * cosf(a));
            v.push_back(cy + r * sinf(a));
        }
    };

    std::vector<float> ringV, eyeLV, eyeRV, smileV;
    // Ring: full circle starting at top (-PI/2), clockwise
    buildArc(ringV, 0.0f, 0.0f, kRingR, kHalfSW,
             -kPI * 0.5f, -kPI * 0.5f + 2.0f * kPI, kRingSegs);
    // Eyes (OpenGL Y-up: positive Y = up)
    buildCircle(eyeLV, -0.35f * kScale,  0.20f * kScale, kEyeR,  kEyeSegs);
    buildCircle(eyeRV,  0.35f * kScale,  0.20f * kScale, kEyeR,  kEyeSegs);
    // Smile: ImGui arc 10deg->170deg, Y flipped -> curves downward in OpenGL Y-up
    buildArc(smileV, 0.0f, 0.0f, kSmileR, kHalfSW,
             10.0f * kPI / 180.0f, 170.0f * kPI / 180.0f, kSmileSegs);

    // --- Upload to GPU ---
    GLuint vao[4], vbo[4];
    glGenVertexArrays(4, vao); glGenBuffers(4, vbo);
    auto upload = [&](int i, const std::vector<float>& v) {
        glBindVertexArray(vao[i]);
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    };
    upload(0, ringV); upload(1, eyeLV); upload(2, eyeRV); upload(3, smileV);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // --- Animation timing (mirrors editor DrawSplash) ---
    const float kDur        = 3.2f;
    const float kRingEnd    = 1.5f;
    const float kEyeStart   = 0.8f,  kEyeEnd   = 1.4f;
    const float kSmileStart = 1.4f,  kSmileEnd  = 2.0f;
    const float kFadeStart  = 2.9f;
    auto clamp01 = [](float v) -> float { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };

    int scrW, scrH;
    glfwGetFramebufferSize(win, &scrW, &scrH);
    float aspect = (float)scrW / std::max(scrH, 1);

    glUseProgram(prog);
    glUniform1f(locAspect, aspect);

    const double startT = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
        float t = (float)(glfwGetTime() - startT);
        if (t >= kDur) break;

        float ringP  = clamp01(t / kRingEnd);
        float eyeA   = clamp01((t - kEyeStart)  / (kEyeEnd  - kEyeStart));
        float smileP = clamp01((t - kSmileStart) / (kSmileEnd - kSmileStart));
        float fadeA  = 1.0f - clamp01((t - kFadeStart) / (kDur - kFadeStart));

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Ring (partial — grows clockwise from top)
        if (ringP > 0.0f) {
            int verts = 2 * ((int)(ringP * kRingSegs) + 1);
            glUniform4f(locColor, 1.0f, 1.0f, 1.0f, fadeA);
            glBindVertexArray(vao[0]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, verts);
        }
        // Eyes (fade in)
        if (eyeA > 0.0f) {
            glUniform4f(locColor, 1.0f, 1.0f, 1.0f, eyeA * fadeA);
            glBindVertexArray(vao[1]);
            glDrawArrays(GL_TRIANGLE_FAN, 0, kEyeSegs + 2);
            glBindVertexArray(vao[2]);
            glDrawArrays(GL_TRIANGLE_FAN, 0, kEyeSegs + 2);
        }
        // Smile (partial arc grows left to right)
        if (smileP > 0.0f) {
            float sAlpha = std::min(smileP * 3.0f, 1.0f) * fadeA;
            int   verts  = 2 * ((int)(smileP * kSmileSegs) + 1);
            glUniform4f(locColor, 1.0f, 1.0f, 1.0f, sAlpha);
            glBindVertexArray(vao[3]);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, verts);
        }

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    // --- Cleanup ---
    glDeleteVertexArrays(4, vao);
    glDeleteBuffers(4, vbo);
    glDeleteProgram(prog);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
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

    // Check for --scene and --windowed launch args
    auto getArg = [&](const std::string& key, const std::string& def) -> std::string {
        std::string p = key + "=";
        for (const auto& a : m_Args)
            if (a.rfind(p, 0) == 0) return a.substr(p.size());
        return def;
    };
    bool hasWindowed = false;
    for (const auto& a : m_Args)
        if (a == "--windowed") { hasWindowed = true; break; }

    std::string sceneArg = getArg("--scene", "");

    if (hasWindowed)
    {
        // Windowed mode for multiplayer testing (side-by-side windows)
        glfwSetWindowSize(m_Window.GetNativeWindow(), 960, 540);
    }
    else
    {
        // Borderless-fullscreen on the primary monitor
        GLFWmonitor*       monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(m_Window.GetNativeWindow(), monitor, 0, 0,
                             vidmode->width, vidmode->height, vidmode->refreshRate);
    }

    // Load game scene
    std::string scenePath = sceneArg.empty() ? "assets/scenes/default.tscene" : sceneArg;
    if (std::filesystem::exists(scenePath))
        SceneSerializer::Load(m_Scene, scenePath);
    ApplyLaunchOverrides();
    SpawnMultiplayerPlayers();

    // Start Lua scripting
    LuaSystem::SetWindow(m_Window.GetNativeWindow());
    LuaSystem::Init(m_Scene);
    LuaSystem::StartScripts();

    // Show the engine splash animation before the game loop begins
    RenderEngineSplash(m_Window.GetNativeWindow());

    // Capture mouse cursor for first-person controls (after splash so cursor is visible during logo)
    glfwSetInputMode(m_Window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);

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
        MultiplayerSystem::Update(m_Scene, deltaTime);
        UpdateTriggers(deltaTime);
        UpdateAnimationControllers(deltaTime);
        UpdateAnimators(deltaTime);
        UpdateSkinnedMeshes(deltaTime);
        UpdateAudio(deltaTime);
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
                pc._HeadbobBaseX   = m_Scene.Transforms[camEnt].Position.x;
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
                m_Scene.Transforms[camEnt].Position.x  = pc._HeadbobBaseX + swayX * 0.25f;
            }
            else
            {
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
                LuaSystem::FireTriggerEnter(i);
            }
        }
        else if (wasInside && !anyPlayerInside)
        {
            if (tr.Channel >= 0)
                m_Scene.SetChannelBool(tr.Channel, tr.OutsideValue);
            LuaSystem::FireTriggerExit(i);
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
            an._Timer += dt * std::max(0.0f, an.PlaybackSpeed);
            if (an._Timer < an.Delay) continue;
            an._Timer  = 0.0f;
            an._DelayDone = true;
        }

        float dur  = std::max(an.Duration, 0.001f);
        an._Timer += dt * std::max(0.0f, an.PlaybackSpeed);
        float rawT = std::min(an._Timer / dur, 1.0f);

        float phaseT = (an.Mode == AnimatorMode::Oscillate && an._Dir < 0.0f)
                       ? 1.0f - rawT : rawT;

        float     easedT = applyEasing(phaseT, an.Easing);
        glm::vec3 target = glm::mix(an.From, an.To, easedT);

        if (i < (int)m_Scene.Transforms.size())
        {
            auto& tr = m_Scene.Transforms[i];
            float w = glm::clamp(an.BlendWeight, 0.0f, 1.0f);
            if      (an.Property == AnimatorProperty::Position) tr.Position = glm::mix(tr.Position, target, w);
            else if (an.Property == AnimatorProperty::Rotation) tr.Rotation = glm::mix(tr.Rotation, target, w);
            else if (an.Property == AnimatorProperty::Scale)    tr.Scale    = glm::mix(tr.Scale,    target, w);
        }

        if (an._Timer >= dur)
        {
            an._Timer = 0.0f;
            if      (an.Mode == AnimatorMode::Oscillate) an._Dir *= -1.0f;
            else if (an.Mode == AnimatorMode::OneShot)   an.Playing = false;
        }
    }
}

void GameApplication::UpdateAnimationControllers(float dt)
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

void GameApplication::UpdateAudio(float)
{
    int camIdx = m_Scene.GetActiveGameCamera();
    if (camIdx < 0) return;
    glm::vec3 listenerPos = m_Scene.GetEntityWorldPos(camIdx);
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
        // Occlusion by barriers
        float occAccum = 0.0f;
        if (d > 0.001f)
        {
            glm::vec3 dir = (listenerPos - pos) / d;
            for (int bi = 0; bi < (int)m_Scene.AudioBarriers.size(); ++bi)
            {
                const auto& b = m_Scene.AudioBarriers[bi];
                if (!b.Active || !b.Enabled) continue;
                glm::vec3 bc = m_Scene.GetEntityWorldPos(bi) + b.Offset;
                glm::vec3 half = b.Size;
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

void GameApplication::UpdateSkinnedMeshes(float)
{
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

} // namespace tsu
