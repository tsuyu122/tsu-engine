#include "ui/uiManager.h"
#include "core/application.h"
#include "renderer/renderer.h"
#include "renderer/textureLoader.h"
#include "renderer/lightmapManager.h"
#include "renderer/mesh.h"
#include "network/multiplayerSystem.h"
#include "serialization/prefabSerializer.h"
#include "scene/entity.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <shlobj.h>
static std::string PickFolderDialog(const char* title)
{
    std::string result;
    BROWSEINFOA bi    = {};
    char         disp[MAX_PATH] = {};
    bi.lpszTitle      = title;
    bi.pszDisplayName = disp;
    bi.ulFlags        = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH] = {};
        if (SHGetPathFromIDListA(pidl, path))
            result = path;
        CoTaskMemFree(pidl);
    }
    return result;
}
#endif

namespace tsu {

void UIManager::Init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Modern dark theme (Godot-inspired)
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // ---- Geometry ----
    style.WindowRounding    = 0.0f;   // sharp window edges â€” modular look
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(6, 4);
    style.ItemInnerSpacing  = ImVec2(4, 4);
    style.IndentSpacing     = 16.0f;
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
    style.WindowPadding     = ImVec2(8, 6);
    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);

    // ---- Palette ----
    // Background tones  (near-black â†’ dark grey)
    const ImVec4 bgDark      (0.11f, 0.11f, 0.13f, 1.00f);   // deepest bg
    const ImVec4 bgMid       (0.14f, 0.14f, 0.16f, 1.00f);   // panels
    const ImVec4 bgLight     (0.18f, 0.18f, 0.21f, 1.00f);   // frames / inputs
    const ImVec4 bgLighter   (0.22f, 0.22f, 0.25f, 1.00f);   // hover frames
    // Accent (calm blue, similar to Godot)
    const ImVec4 accent      (0.26f, 0.56f, 0.98f, 1.00f);
    const ImVec4 accentDim   (0.20f, 0.42f, 0.78f, 0.70f);
    const ImVec4 accentBright(0.38f, 0.66f, 1.00f, 1.00f);
    // Text
    const ImVec4 textNorm    (0.86f, 0.87f, 0.88f, 1.00f);
    const ImVec4 textDim     (0.50f, 0.52f, 0.56f, 1.00f);
    // Borders
    const ImVec4 border      (0.24f, 0.24f, 0.28f, 1.00f);
    const ImVec4 borderLight (0.32f, 0.32f, 0.38f, 1.00f);
    // Tab bar
    const ImVec4 tabBg       (0.12f, 0.12f, 0.14f, 1.00f);
    const ImVec4 tabActive   (0.18f, 0.18f, 0.22f, 1.00f);
    const ImVec4 tabHover    (0.22f, 0.22f, 0.28f, 1.00f);

    auto& c = style.Colors;
    c[ImGuiCol_Text]                  = textNorm;
    c[ImGuiCol_TextDisabled]          = textDim;
    c[ImGuiCol_WindowBg]              = bgMid;
    c[ImGuiCol_ChildBg]               = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_PopupBg]               = ImVec4(0.12f, 0.12f, 0.14f, 0.96f);
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_FrameBg]               = bgLight;
    c[ImGuiCol_FrameBgHovered]        = bgLighter;
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.26f, 0.26f, 0.30f, 1.0f);
    c[ImGuiCol_TitleBg]               = bgDark;
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.14f, 0.14f, 0.17f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]      = bgDark;
    c[ImGuiCol_MenuBarBg]             = bgDark;
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.10f, 0.10f, 0.12f, 0.6f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.30f, 0.30f, 0.35f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.40f, 0.40f, 0.46f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accentDim;
    c[ImGuiCol_SliderGrabActive]      = accent;
    c[ImGuiCol_Button]                = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);
    c[ImGuiCol_ButtonHovered]         = ImVec4(0.30f, 0.30f, 0.36f, 1.0f);
    c[ImGuiCol_ButtonActive]          = accent;
    c[ImGuiCol_Header]                = ImVec4(0.22f, 0.22f, 0.26f, 1.0f);
    c[ImGuiCol_HeaderHovered]         = ImVec4(0.28f, 0.28f, 0.34f, 1.0f);
    c[ImGuiCol_HeaderActive]          = accentDim;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accentDim;
    c[ImGuiCol_SeparatorActive]       = accent;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.26f, 0.56f, 0.98f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]     = ImVec4(0.26f, 0.56f, 0.98f, 0.67f);
    c[ImGuiCol_ResizeGripActive]      = accent;
    c[ImGuiCol_Tab]                   = tabBg;
    c[ImGuiCol_TabHovered]            = tabHover;
    c[ImGuiCol_TabActive]             = tabActive;
    c[ImGuiCol_TabUnfocused]          = tabBg;
    c[ImGuiCol_TabUnfocusedActive]    = tabActive;
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_TableHeaderBg]         = ImVec4(0.16f, 0.16f, 0.19f, 1.0f);
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = ImVec4(0.20f, 0.20f, 0.24f, 1.0f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1.0f, 1.0f, 1.0f, 0.02f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
}

void UIManager::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

// ---------------------------------------------------------------------------
// DrawEngineLogo  â€”  smiley-face logo drawn via ImDrawList
//   cx/cy    : screen-space centre
//   sz       : bounding square size (maps SVG viewBox 200x200 â†’ szÃ—sz)
//   col      : base colour (fully opaque), used with per-part alpha overrides
//   ringProgress  : 0..1 how much of the outer ring is drawn (stroke)
//   eyeAlpha      : 0..1 opacity of the two eye dots
//   smileProgress : 0..1 how much of the smile arc is drawn
// ---------------------------------------------------------------------------
void UIManager::DrawEngineLogo(ImDrawList* dl, float cx, float cy,
                               float sz,  unsigned int /*col*/,
                               float ringProgress,
                               float eyeAlpha,
                               float smileProgress)
{
    static constexpr float kPi  = 3.14159265358979f;
    const float s    = sz / 200.0f;
    const float str  = std::max(1.0f, 12.0f * s);  // stroke width

    // --- Outer ring (partial arc drawn from top, clockwise) ---
    if (ringProgress > 0.0f)
    {
        float aMin = -kPi * 0.5f;                        // start at top
        float aMax = aMin + ringProgress * 2.0f * kPi;  // clockwise
        const int   segs  = 96;
        const ImU32 white = IM_COL32(255, 255, 255, 255);
        dl->PathClear();
        dl->PathArcTo(ImVec2(cx, cy), 90.0f * s, aMin, aMax, segs);
        dl->PathStroke(white, false, str);
    }

    // --- Eyes ---
    if (eyeAlpha > 0.0f)
    {
        const ImU32 eCol = IM_COL32(255, 255, 255, (int)(eyeAlpha * 255.0f));
        dl->AddCircleFilled(ImVec2(cx - 35.0f * s, cy - 20.0f * s), 14.0f * s, eCol, 32);
        dl->AddCircleFilled(ImVec2(cx + 35.0f * s, cy - 20.0f * s), 14.0f * s, eCol, 32);
    }

    // --- Smile arc (centre â‰ˆ 100,100 in SVG, r=60, 10Â°â†’170Â°) ---
    // In ImGui Y-down coords: 0Â°=right, 90Â°=down  â†’ arc 10Â°..170Â° is the bottom smile curve.
    if (smileProgress > 0.0f)
    {
        const float aSmileMin = 10.0f  * kPi / 180.0f;
        const float aSmileMax = aSmileMin + smileProgress * (160.0f * kPi / 180.0f);
        const ImU32 sCol = IM_COL32(255, 255, 255,
                                    (int)(std::min(smileProgress * 3.0f, 1.0f) * 255.0f));
        dl->PathClear();
        dl->PathArcTo(ImVec2(cx, cy), 60.0f * s, aSmileMin, aSmileMax, 48);
        dl->PathStroke(sCol, false, str);
    }
}

bool UIManager::Render(Scene& scene, int& selectedEntity, int winW, int winH,
                       std::vector<std::string>& droppedFiles, GizmoMode& gizmoMode)
{
    // ---- Splash screen (runs for first 3.2 seconds) ----
    {
        if (m_SplashStart < 0.0f)
            m_SplashStart = (float)ImGui::GetTime();

        float t = (float)ImGui::GetTime() - m_SplashStart;
        if (t < 3.2f)
        {
            DrawSplash(t, winW, winH);
            return true; // capture all input during splash
        }
    }
    const int topMenuH  = k_MenuBarH;
    const int topToolH  = k_ToolbarH;
    const int topTabsH  = k_TopTabsH;
    const int topStackH = k_TopStackH;

    // Main menu bar (File/Edit)
    if (ImGui::BeginMainMenuBar())
    {
        // ---- Engine logo (top-left, before menu items) ----
        {
            const float barH = (float)k_MenuBarH;
            const float sz   = (barH - 4.0f) * 0.70f;  // 70% of full height â€” less obtrusive
            ImDrawList* dl   = ImGui::GetWindowDrawList();
            ImVec2      pos  = ImGui::GetCursorScreenPos();
            float cx = pos.x + sz * 0.5f;
            float cy = pos.y + (barH - 4.0f) * 0.5f;
            DrawEngineLogo(dl, cx, cy, sz, IM_COL32(255,255,255,255));
            ImGui::Dummy(ImVec2(sz + 6.0f, barH - 4.0f));
        }
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New Project..."))
            { m_DlgProjName[0] = '\0'; m_DlgProjParent[0] = '\0'; m_ShowNewProjectDlg = true; }
            if (ImGui::MenuItem("Open Project..."))
            { m_DlgOpenPath[0] = '\0'; m_ShowOpenProjectDlg = true; }
            if (ImGui::MenuItem("Save Project", "Ctrl+S"))
                m_PendingSaveProject = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Export Game..."))
            {
                m_DlgExportPath[0] = '\0';
                strncpy(m_DlgGameName, m_ProjectDisplayName.c_str(), sizeof(m_DlgGameName) - 1);
                m_DlgGameName[sizeof(m_DlgGameName) - 1] = '\0';
                m_ShowExportGameDlg = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Project Settings"))
            {
                m_ProjectSettingsOpen = true;
                m_RequestViewportTab = 2;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Run"))
        {
            if (ImGui::MenuItem("Multiplayer Local Test..."))
                m_MultiplayerTestWindowOpen = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            if (ImGui::MenuItem("Realtime Graphics"))
            {
                m_RealtimeWindowOpen = true;
                m_RequestViewportTab = 6;
            }
            if (ImGui::MenuItem("Baked Lighting"))
            {
                m_BakedLightingWindowOpen = true;
                m_RequestViewportTab = 7;
            }
            if (ImGui::MenuItem("Procedural Maze"))
            {
                m_MazeWindowOpen = true;
                m_RequestViewportTab = 4;
            }
            if (ImGui::MenuItem("Entity Network HUD", nullptr, m_ShowEntityNetHUD))
            {
                m_ShowEntityNetHUD = !m_ShowEntityNetHUD;
            }
            ImGui::EndMenu();
        }

        // Right side: project name
        {
            std::string label = "\xf0\x9f\x97\x80 " + m_ProjectDisplayName;
            float labelW = ImGui::CalcTextSize(label.c_str()).x + 20.0f;
            float avail  = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - labelW);
            ImGui::TextDisabled("%s", label.c_str());
        }

        ImGui::EndMainMenuBar();
    }

    // ---- Project dialog modals (triggered by File menu) ----
    if (m_ShowNewProjectDlg)  { ImGui::OpenPopup("New Project##dlg");  m_ShowNewProjectDlg  = false; }
    if (m_ShowOpenProjectDlg) { ImGui::OpenPopup("Open Project##dlg"); m_ShowOpenProjectDlg = false; }
    if (m_ShowExportGameDlg)  { ImGui::OpenPopup("Export Game##dlg");  m_ShowExportGameDlg  = false; }

    // New Project
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("New Project##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Project Name:");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##pname", m_DlgProjName, sizeof(m_DlgProjName));
        ImGui::Spacing();
        ImGui::TextUnformatted("Location (parent folder):");
        {
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(bw - 90.0f);
            ImGui::InputText("##ploc", m_DlgProjParent, sizeof(m_DlgProjParent));
            ImGui::SameLine();
#ifdef _WIN32
            if (ImGui::Button("Browse...##np", ImVec2(85.0f, 0))) {
                auto r = PickFolderDialog("Select project location");
                if (!r.empty()) {
                    strncpy(m_DlgProjParent, r.c_str(), sizeof(m_DlgProjParent) - 1);
                    m_DlgProjParent[sizeof(m_DlgProjParent) - 1] = '\0';
                }
            }
#endif
        }
        ImGui::TextDisabled("A sub-folder <Name> will be created at this path.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            if (m_DlgProjName[0] != '\0') { m_PendingNewProject = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Open Project
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Open Project##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Project folder:");
        {
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(bw - 90.0f);
            ImGui::InputText("##opath", m_DlgOpenPath, sizeof(m_DlgOpenPath));
            ImGui::SameLine();
#ifdef _WIN32
            if (ImGui::Button("Browse...##op", ImVec2(85.0f, 0))) {
                auto r = PickFolderDialog("Select project folder");
                if (!r.empty()) {
                    strncpy(m_DlgOpenPath, r.c_str(), sizeof(m_DlgOpenPath) - 1);
                    m_DlgOpenPath[sizeof(m_DlgOpenPath) - 1] = '\0';
                }
            }
#endif
        }
        ImGui::TextDisabled("You can also type a path to a .tsproj file directly.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (m_DlgOpenPath[0] != '\0') { m_PendingOpenProject = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Export Game
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Export Game##dlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Game Name:");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##gname", m_DlgGameName, sizeof(m_DlgGameName));
        ImGui::Spacing();
        ImGui::TextUnformatted("Version:");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##gver", m_DlgGameVersion, sizeof(m_DlgGameVersion));
        ImGui::Spacing();
        ImGui::TextUnformatted("Author:");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##gauth", m_DlgGameAuthor, sizeof(m_DlgGameAuthor));
        ImGui::Spacing();
        ImGui::TextUnformatted("Output folder:");
        {
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::SetNextItemWidth(bw - 90.0f);
            ImGui::InputText("##epath", m_DlgExportPath, sizeof(m_DlgExportPath), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
#ifdef _WIN32
            if (ImGui::Button("Browse...##eg", ImVec2(85.0f, 0))) {
                auto r = PickFolderDialog("Select output folder");
                if (!r.empty()) {
                    strncpy(m_DlgExportPath, r.c_str(), sizeof(m_DlgExportPath) - 1);
                    m_DlgExportPath[sizeof(m_DlgExportPath) - 1] = '\0';
                }
            }
#endif
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("Cover image path (.png)");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputText("##gcover", m_DlgGameCover, sizeof(m_DlgGameCover));
        ImGui::TextDisabled("The game .exe and assets/ will be placed in this folder.");
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Export", ImVec2(120, 0))) {
            if (m_DlgExportPath[0] != '\0') { m_PendingExportGame = true; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // --- Process externally dropped files (this function knows m_CurrentFolder) ---
    {
        static const char* imgExts[] = {".png",".jpg",".jpeg",".bmp",".tga",".hdr",nullptr};
        for (auto& p : droppedFiles)
        {
            size_t dp = p.find_last_of('.');
            if (dp == std::string::npos) continue;
            std::string ext = p.substr(dp);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);

            // ---- OBJ file: register as asset only (no entity spawned) ----
            if (ext == ".obj")
            {
                bool alreadyAsset = false;
                for (const auto& ma : scene.MeshAssets)
                    if (ma == p) { alreadyAsset = true; break; }
                if (!alreadyAsset) {
                    scene.MeshAssets.push_back(p);
                    scene.MeshAssetFolders.push_back(m_CurrentFolder);
                }
                continue;
            }

            // ---- Image file: register as texture ----
            bool isImg = false;
            for (int k = 0; imgExts[k]; ++k) if (ext == imgExts[k]) { isImg = true; break; }
            if (!isImg) continue;
            if (std::find(scene.Textures.begin(), scene.Textures.end(), p) != scene.Textures.end()) continue;
            scene.Textures.push_back(p);
            scene.TextureIDs.push_back(tsu::LoadTexture(p));
            scene.TextureFolders.push_back(m_CurrentFolder);
            scene.TextureSettings.emplace_back();
        }
        droppedFiles.clear();
    }

    // Side panels start just below the main menu bar, filling the full height
    // minus the bottom asset panel.
    const int sideY = topMenuH;
    const int sideH = winH - k_AssetH - sideY;

    // --- Panel resize handles (run every frame before panels are drawn) ---
    {
        const float handleW = 6.0f;
        const float hy0 = (float)sideY;
        const float hy1 = (float)(winH - k_AssetH);
        const float lx  = (float)m_LeftPanelWidth;
        const float rx  = (float)(winW - m_RightPanelWidth);
        ImGuiIO& rio = ImGui::GetIO();
        float mx2 = rio.MousePos.x;
        float my2 = rio.MousePos.y;
        bool nearLeft  = (mx2>=lx-handleW && mx2<=lx+handleW && my2>hy0 && my2<hy1);
        bool nearRight = (mx2>=rx-handleW && mx2<=rx+handleW && my2>hy0 && my2<hy1);

        // Only begin resize on the frame mouse is first clicked (not held)
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !rio.WantCaptureMouse)
        {
            if (nearLeft  && !m_DraggingRight) m_DraggingLeft  = true;
            if (nearRight && !m_DraggingLeft)  m_DraggingRight = true;
        }
        if (!rio.MouseDown[0]) { m_DraggingLeft = false; m_DraggingRight = false; }
        if (m_DraggingLeft)
        {
            int nw = (int)mx2;
            m_LeftPanelWidth = nw < 150 ? 150 : (nw > winW/3 ? winW/3 : nw);
        }
        if (m_DraggingRight)
        {
            int nw = winW - (int)mx2;
            m_RightPanelWidth = nw < 150 ? 150 : (nw > winW/3 ? winW/3 : nw);
        }
        if (nearLeft || nearRight || m_DraggingLeft || m_DraggingRight)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    // ---- Prefab editor state (needed by hierarchy + inspector) ----
    bool prefabEditorActive = (m_ViewportTab == 3 && m_PrefabEditorOpen &&
                               m_EditingPrefabIdx >= 0 &&
                               m_EditingPrefabIdx < (int)scene.Prefabs.size());

    // Hierarchy
    if (prefabEditorActive)
    {
        int editIdx = m_EditingPrefabIdx;
        m_Hierarchy.SetPrefabCallbacks(
            // Sync Instances callback
            [&scene, editIdx]() {
                PrefabAsset& editPrefab = scene.Prefabs[editIdx];
                for (int ei = 0; ei < (int)scene.EntityPrefabSource.size(); ++ei)
                {
                    if (scene.EntityPrefabSource[ei] != editIdx) continue;
                    if (editPrefab.Nodes.empty()) continue;

                    bool isRootInstance = true;
                    int parentEnt = (ei < (int)scene.EntityParents.size()) ? scene.EntityParents[ei] : -1;
                    if (parentEnt >= 0 && parentEnt < (int)scene.EntityPrefabSource.size())
                        if (scene.EntityPrefabSource[parentEnt] == editIdx)
                            isRootInstance = false;

                    int nodeIdx = isRootInstance ? 0 : -1;
                    if (!isRootInstance) {
                        for (int n = 0; n < (int)editPrefab.Nodes.size(); ++n)
                            if (editPrefab.Nodes[n].Name == scene.EntityNames[ei]) { nodeIdx = n; break; }
                    }
                    if (nodeIdx < 0 || nodeIdx >= (int)editPrefab.Nodes.size()) continue;

                    const PrefabEntityData& node = editPrefab.Nodes[nodeIdx];
                    if (!isRootInstance)
                        scene.Transforms[ei].Position = node.Transform.Position;
                    scene.Transforms[ei].Rotation = node.Transform.Rotation;
                    scene.Transforms[ei].Scale    = node.Transform.Scale;
                    scene.EntityNames[ei]         = node.Name;

                    if (!node.MeshType.empty() && scene.MeshRenderers[ei].MeshType != node.MeshType) {
                        Mesh* mesh = nullptr;
                        const std::string& mt = node.MeshType;
                        if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
                        else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
                        else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
                        else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
                        else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
                        else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
                        else if (mt.size() > 4 && mt.substr(0,4) == "obj:") mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
                        if (mesh) {
                            delete scene.MeshRenderers[ei].MeshPtr;
                            scene.MeshRenderers[ei].MeshPtr  = mesh;
                            scene.MeshRenderers[ei].MeshType = mt;
                        }
                    }

                    if (!node.MaterialName.empty()) {
                        for (int mi2 = 0; mi2 < (int)scene.Materials.size(); ++mi2)
                            if (scene.Materials[mi2].Name == node.MaterialName) { scene.EntityMaterial[ei] = mi2; break; }
                    }

                    if (node.HasLight) scene.Lights[ei] = node.Light;
                }
            },
            // Save to Disk callback
            [&scene, editIdx]() {
                PrefabAsset& editPrefab = scene.Prefabs[editIdx];
                if (!editPrefab.FilePath.empty())
                    PrefabSerializer::Save(editPrefab, editPrefab.FilePath);
            }
        );
        m_Hierarchy.SetPrefabEditorState(true, m_EditingPrefabIdx, &m_PrefabSelectedNode);
        m_Hierarchy.SetPrefabOverride(nullptr);
    }
    else
    {
        m_Hierarchy.SetPrefabEditorState(false, -1, nullptr);
        m_Hierarchy.SetPrefabOverride(nullptr);
    }

    // ---- Room editor: piggyback on prefab editor with override pointer ----
    bool roomEditorActive = (m_ViewportTab == 5 && m_MazeRoomEditorOpen &&
                             m_EditingRoomSetIdx >= 0 &&
                             m_EditingRoomSetIdx < (int)scene.RoomSets.size() &&
                             m_EditingRoomIdx >= 0 &&
                             m_EditingRoomIdx < (int)scene.RoomSets[m_EditingRoomSetIdx].Rooms.size());

    if (roomEditorActive)
    {
        RoomTemplate& editRoom = scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx];
        m_RoomAsPrefab.Name  = editRoom.Name;
        m_RoomAsPrefab.Nodes = editRoom.Interior;  // copy in

        m_Hierarchy.SetPrefabEditorState(true, -1, &m_RoomSelectedNode);
        m_Hierarchy.SetPrefabOverride(&m_RoomAsPrefab);
    }

    // ---------------------------------------------------------------
    // Panel Docking Tab System
    // ---------------------------------------------------------------
    static constexpr float k_SlotTabH = 26.0f;
    const char* dockPanelNames[2] = { "Hierarchy", "Inspector" };

    // Count panels per slot
    int leftPanelCnt = 0, rightPanelCnt = 0;
    int leftPanelIds[2] = {-1,-1}, rightPanelIds[2] = {-1,-1};
    for (int p = 0; p < 2; p++) {
        if (m_PanelSlot[p] == 0)
            leftPanelIds[leftPanelCnt++] = p;
        else
            rightPanelIds[rightPanelCnt++] = p;
    }

    int leftActivePanel = -1, rightActivePanel = -1;

    // Lambda: render a slot's tab bar and return the active panel ID
    auto drawSlotTabs = [&](int slotId, float sx, float sy, float sw,
                            int* slotPanels, int panelCnt) -> int
    {
        if (panelCnt == 0) return -1;

        char wid[32]; snprintf(wid, sizeof(wid), "##slottab%d", slotId);
        ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(sw, k_SlotTabH), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 2));
        ImGui::Begin(wid, nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);

        int activePanel = slotPanels[0];

        char tabBarId[32]; snprintf(tabBarId, sizeof(tabBarId), "##stb%d", slotId);
        if (ImGui::BeginTabBar(tabBarId))
        {
            for (int i = 0; i < panelCnt; i++)
            {
                int pid = slotPanels[i];
                if (ImGui::BeginTabItem(dockPanelNames[pid]))
                {
                    activePanel = pid;
                    ImGui::EndTabItem();
                }
                // Begin drag on tab
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 8.0f))
                {
                    m_DraggingPanel = true;
                    m_DragPanelId   = pid;
                }
                // Right-click context menu
                if (ImGui::BeginPopupContextItem())
                {
                    const char* moveLabel = (slotId == 0)
                        ? "Move to Right" : "Move to Left";
                    if (ImGui::MenuItem(moveLabel))
                        m_PanelSlot[pid] = 1 - slotId;
                    ImGui::EndPopup();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::PopStyleVar();
        ImGui::End();
        return activePanel;
    };

    // Render tab bars for both slots
    leftActivePanel  = drawSlotTabs(0, 0, (float)sideY,
                                    (float)m_LeftPanelWidth,
                                    leftPanelIds, leftPanelCnt);
    rightActivePanel = drawSlotTabs(1, (float)(winW - m_RightPanelWidth), (float)sideY,
                                    (float)m_RightPanelWidth,
                                    rightPanelIds, rightPanelCnt);

    // Handle panel dragging (floating label + release to dock)
    if (m_DraggingPanel)
    {
        ImVec2 mp = ImGui::GetMousePos();
        ImGui::SetNextWindowPos(ImVec2(mp.x + 12, mp.y + 4));
        ImGui::SetNextWindowBgAlpha(0.8f);
        ImGui::Begin("##dragpanel", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::TextUnformatted(dockPanelNames[m_DragPanelId]);
        ImGui::End();

        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            int targetSlot = (mp.x < winW * 0.5f) ? 0 : 1;
            m_PanelSlot[m_DragPanelId] = targetSlot;
            m_DraggingPanel = false;
            m_DragPanelId   = -1;
        }
    }

    // Compute panel positions based on docking slot
    int hierSlot = m_PanelSlot[0];
    bool hierVisible = (hierSlot == 0) ? (leftActivePanel == 0)
                                       : (rightActivePanel == 0);
    int hierX = (hierSlot == 0) ? 0 : (winW - m_RightPanelWidth);
    int hierW = (hierSlot == 0) ? m_LeftPanelWidth : m_RightPanelWidth;

    int inspSlot = m_PanelSlot[1];
    bool inspVisible = (inspSlot == 0) ? (leftActivePanel == 1)
                                       : (rightActivePanel == 1);
    int inspX = (inspSlot == 0) ? 0 : (winW - m_RightPanelWidth);
    int inspW = (inspSlot == 0) ? m_LeftPanelWidth : m_RightPanelWidth;

    const int tabOff = (int)k_SlotTabH;

    // Hierarchy
    if (hierVisible)
        m_Hierarchy.Render(scene, selectedEntity,
                           hierX, sideY + tabOff, hierW, sideH - tabOff);

    // Sync hierarchy changes back to room Interior
    if (roomEditorActive)
    {
        RoomTemplate& editRoom = scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx];
        editRoom.Interior = m_RoomAsPrefab.Nodes;
        if (m_RoomSelectedNode >= (int)m_RoomAsPrefab.Nodes.size())
            m_RoomSelectedNode = (int)m_RoomAsPrefab.Nodes.size() - 1;
    }

    int selectedGroup = m_Hierarchy.GetSelectedGroup();

    // Clear asset-browser selections when hierarchy has an entity/group selected
    if (selectedEntity >= 0 || selectedGroup >= 0)
    {
        m_SelectedMaterial = -1;
        m_SelectedTexture  = -1;
        m_SelectedPrefab   = -1;
        m_SelectedFolder   = -1;
    }

    // Inspector
    int inspMat = (selectedEntity < 0 && selectedGroup < 0) ? m_SelectedMaterial : -1;
    int inspTex = (selectedEntity < 0 && selectedGroup < 0) ? m_SelectedTexture  : -1;
    int inspPrf = (selectedEntity < 0 && selectedGroup < 0) ? m_SelectedPrefab   : -1;
    const std::vector<int>* multiSel = &m_Hierarchy.GetMultiSelection();

    if (roomEditorActive)
    {
        m_Inspector.SetPrefabEditorState(true, -1, m_RoomSelectedNode);
        m_Inspector.SetPrefabOverride(&m_RoomAsPrefab);
    }
    else
    {
        m_Inspector.SetPrefabEditorState(prefabEditorActive, m_EditingPrefabIdx, m_PrefabSelectedNode);
        m_Inspector.SetPrefabOverride(nullptr);
    }
    // Room editor: shrink inspector so lightmap panel doesn't overlap
    const float lmPanelH = roomEditorActive ? 260.0f : 0.0f;
    if (inspVisible)
        m_Inspector.Render(scene, selectedEntity, selectedGroup,
                           inspX, sideY + tabOff, inspW,
                           sideH - tabOff - (int)lmPanelH,
                           inspMat, inspTex, inspPrf, multiSel);

    // Sync inspector changes back to room Interior
    if (roomEditorActive)
    {
        RoomTemplate& editRoom = scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx];
        editRoom.Interior = m_RoomAsPrefab.Nodes;
    }

    // ---------------------------------------------------------------
    // Lightmap inspector panel (visible when room editor is active)
    // ---------------------------------------------------------------
    if (roomEditorActive)
    {
        const float lmpX = (float)inspX;
        const float lmpY = (float)(sideY + sideH) - lmPanelH;
        const float lmpW = (float)inspW;

        ImGui::SetNextWindowPos (ImVec2(lmpX, lmpY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(lmpW, lmPanelH), ImGuiCond_Always);
        ImGui::Begin("##lmInspector", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar);

        RoomTemplate& editRoom =
            scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx];

        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Lightmap");
        ImGui::Separator();

        if (editRoom.LightmapPath.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Not baked");
        }
        else
        {
            // Truncate long path for display
            std::string displayPath = editRoom.LightmapPath;
            if (displayPath.size() > 34)
                displayPath = "..." + displayPath.substr(displayPath.size() - 31);
            ImGui::TextColored(ImVec4(0.50f, 1.0f, 0.50f, 1.0f), "%s", displayPath.c_str());

            unsigned int lmId = LightmapManager::Instance().GetCachedID(editRoom.LightmapPath);
            if (lmId == 0)
            {
                // Not loaded yet (e.g. a bake was done in a previous session); load on demand
                lmId = LightmapManager::Instance().Load(editRoom.LightmapPath);
            }

            if (lmId != 0)
            {
                // Calculate preview size (square, fits remaining width minus padding)
                const float prevSz = std::min(lmpW - 16.0f, lmPanelH - 72.0f);
                ImGui::Image((ImTextureID)(intptr_t)lmId, ImVec2(prevSz, prevSz),
                             ImVec2(0, 0), ImVec2(1, 1));
            }
            else
            {
                ImGui::TextDisabled("(texture not available)");
            }

            ImGui::Spacing();
            if (ImGui::SmallButton("Clear Lightmap"))
            {
                LightmapManager::Instance().Release(editRoom.LightmapPath);
                editRoom.LightmapPath.clear();
            }
        }

        ImGui::End();
    }

    // ---------------------------------------------------------------
    // Top panel: toolbar + camera tabs (spans viewport width)
    // ---------------------------------------------------------------
    {
        const float topH = (float)(topToolH + topTabsH);
        const float y = (float)(topMenuH);
        const float panelX = (float)m_LeftPanelWidth;
        const float panelW = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        ImGui::SetNextWindowPos (ImVec2(panelX, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panelW, topH), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.11f, 0.13f, 1.0f));
        ImGui::Begin("##cameratop", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar);

        // Gizmo mode buttons (Move / Rotate / Scale) on the left
        {
            float btnSz = 26.0f;
            auto gizmoBtn = [&](const char* label, GizmoMode mode) {
                bool active = (gizmoMode == mode);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.26f, 0.56f, 0.98f, 0.85f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.33f, 0.63f, 1.00f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(btnSz, btnSz)))
                    gizmoMode = mode;
                ImGui::PopStyleColor(2);
            };
            gizmoBtn("W", GizmoMode::Move);
            ImGui::SameLine(0, 2);
            gizmoBtn("E", GizmoMode::Scale);
            ImGui::SameLine(0, 2);
            gizmoBtn("R", GizmoMode::Rotate);
            ImGui::SameLine(0, 10);

            // Post-processing toggle (editor viewport only)
            if (m_ViewportTab == 0)
            {
                bool postOn = Renderer::s_EditorPostEnabled;
                if (postOn) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.62f, 0.38f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.72f, 0.46f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
                }
                if (ImGui::Button("Post", ImVec2(btnSz * 2.0f, btnSz)))
                    Renderer::s_EditorPostEnabled = !Renderer::s_EditorPostEnabled;
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle post-processing on the editor viewport");
                ImGui::SameLine(0, 10);
            }
        }

        // ---- Play / Pause buttons (centered) ----
        {
            float playBtnW = 60.0f;
            float pauseBtnW = 60.0f;
            float gap = 6.0f;
            float totalBtnW = playBtnW + pauseBtnW + gap;
            float centerX = panelW * 0.5f - totalBtnW * 0.5f;
            ImGui::SameLine(centerX);

            // Play / Stop button
            if (m_IsPlaying) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.18f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.90f, 0.28f, 0.28f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.62f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.74f, 0.38f, 1.0f));
            }
            if (ImGui::Button(m_IsPlaying ? "Stop" : "Play", ImVec2(playBtnW, 26.0f)))
                m_PendingPlayToggle = true;
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(m_IsPlaying ? "Stop and return to editor (F1)" : "Play the game (F1)");

            ImGui::SameLine(0, gap);

            // Pause button
            if (m_IsPaused) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.75f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.85f, 0.25f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.22f, 0.24f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.32f, 0.80f, 1.0f));
            }
            bool pauseDisabled = !m_IsPlaying;
            if (pauseDisabled) ImGui::BeginDisabled();
            if (ImGui::Button(m_IsPaused ? "Resume" : "Pause", ImVec2(pauseBtnW, 26.0f)))
                m_PendingPauseToggle = true;
            if (pauseDisabled) ImGui::EndDisabled();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered() && !pauseDisabled)
                ImGui::SetTooltip(m_IsPaused ? "Resume game (F2)" : "Pause game (F2)");
        }

        if (ImGui::BeginTabBar("##cameratabs"))
        {
            const int reqTab = m_RequestViewportTab;
            if (ImGui::BeginTabItem("Editor Camera", nullptr,
                (reqTab == 0) ? ImGuiTabItemFlags_SetSelected : 0))
            {
                m_ViewportTab = 0;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Game Camera", nullptr,
                (reqTab == 1) ? ImGuiTabItemFlags_SetSelected : 0))
            {
                m_ViewportTab = 1;
                ImGui::EndTabItem();
            }

            if (m_ProjectSettingsOpen)
            {
                bool keepOpen = true;
                if (ImGui::BeginTabItem("Project Settings", &keepOpen,
                    (reqTab == 2) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 2;
                    ImGui::EndTabItem();
                }
                if (!keepOpen)
                {
                    m_ProjectSettingsOpen = false;
                    if (m_ViewportTab == 2) m_ViewportTab = 0;
                }
            }

            // Prefab editor tab (closeable, named after the prefab)
            if (m_PrefabEditorOpen && m_EditingPrefabIdx >= 0 &&
                m_EditingPrefabIdx < (int)scene.Prefabs.size())
            {
                bool keepPrefab = true;
                std::string tabName = scene.Prefabs[m_EditingPrefabIdx].Name + "##prefabtab";
                if (ImGui::BeginTabItem(tabName.c_str(), &keepPrefab,
                    (reqTab == 3) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 3;
                    ImGui::EndTabItem();
                }
                if (!keepPrefab)
                {
                    m_PrefabEditorOpen = false;
                    m_EditingPrefabIdx = -1;
                    if (m_ViewportTab == 3) m_ViewportTab = 0;
                }
            }

            // Maze overview tab (closeable, tab index 4)
            if (m_MazeWindowOpen)
            {
                bool keepMaze = true;
                if (ImGui::BeginTabItem("Procedural Maze##mazetab", &keepMaze,
                    (reqTab == 4) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 4;
                    ImGui::EndTabItem();
                }
                if (!keepMaze)
                {
                    m_MazeWindowOpen = false;
                    m_MazeRoomEditorOpen = false;
                    m_EditingRoomSetIdx  = -1;
                    m_EditingRoomIdx     = -1;
                    if (m_ViewportTab == 4 || m_ViewportTab == 5) m_ViewportTab = 0;
                }
            }

            // Room editor tab (closeable, named after the room, tab index 5)
            if (m_MazeRoomEditorOpen && m_EditingRoomSetIdx >= 0 &&
                m_EditingRoomSetIdx < (int)scene.RoomSets.size() &&
                m_EditingRoomIdx >= 0 &&
                m_EditingRoomIdx < (int)scene.RoomSets[m_EditingRoomSetIdx].Rooms.size())
            {
                bool keepRoom = true;
                std::string roomTabName = scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx].Name + "##roomtab";
                if (ImGui::BeginTabItem(roomTabName.c_str(), &keepRoom,
                    (reqTab == 5) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 5;
                    ImGui::EndTabItem();
                }
                if (!keepRoom)
                {
                    m_MazeRoomEditorOpen = false;
                    m_EditingRoomSetIdx  = -1;
                    m_EditingRoomIdx     = -1;
                    if (m_ViewportTab == 5) m_ViewportTab = 0;
                }
            }

            // Realtime Graphics tab (closeable, tab index 6)
            if (m_RealtimeWindowOpen)
            {
                bool keepRt = true;
                if (ImGui::BeginTabItem("Realtime Graphics##rttab", &keepRt,
                    (reqTab == 6) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 6;
                    ImGui::EndTabItem();
                }
                if (!keepRt)
                {
                    m_RealtimeWindowOpen = false;
                    if (m_ViewportTab == 6) m_ViewportTab = 0;
                }
            }

            // Baked Lighting tab (closeable, tab index 7)
            if (m_BakedLightingWindowOpen)
            {
                bool keepBk = true;
                if (ImGui::BeginTabItem("Baked Lighting##bakedtab", &keepBk,
                    (reqTab == 7) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 7;
                    ImGui::EndTabItem();
                }
                if (!keepBk)
                {
                    m_BakedLightingWindowOpen = false;
                    if (m_ViewportTab == 7) m_ViewportTab = 0;
                }
            }

            // Animation Controller tab (closeable, tab index 8)
            if (m_AnimControllerWindowOpen)
            {
                bool keepAc = true;
                if (ImGui::BeginTabItem("Animation Controller##actab", &keepAc,
                    (reqTab == 8) ? ImGuiTabItemFlags_SetSelected : 0))
                {
                    m_ViewportTab = 8;
                    ImGui::EndTabItem();
                }
                if (!keepAc)
                {
                    m_AnimControllerWindowOpen = false;
                    if (m_ViewportTab == 8) m_ViewportTab = 0;
                }
            }

            m_RequestViewportTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::PopStyleColor(); // WindowBg
    }

    // Project settings view (third viewport mode)
    if (m_ViewportTab == 2)
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);

        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        // (theme-managed bg)
        ImGui::Begin("##projectsettings_view", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

        // Left: category list
        ImGui::BeginChild("##ps_catlist", ImVec2(180.0f, 0), true);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.62f, 0.68f, 1.0f));
        ImGui::TextUnformatted("Settings");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Selectable("Channel System", m_PsCategory == 0))
            m_PsCategory = 0;
        // (future categories can be added here)
        ImGui::EndChild();

        ImGui::SameLine();

        // Right: content area
        ImGui::BeginChild("##ps_content", ImVec2(0, 0), false,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (m_PsCategory == 0)
        {
            // ---- Channel System ----
            float addBtnW = 106.0f;
            ImGui::TextColored(ImVec4(0.75f, 0.82f, 1.0f, 1.0f), "Channel System");
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - addBtnW);
            if (ImGui::Button("+ Add Channel", ImVec2(addBtnW, 0)))
            {
                ChannelVariable nch;
                nch.Name = "Channel " + std::to_string(scene.Channels.size() + 1);
                scene.Channels.push_back(nch);
            }
            ImGui::Separator();

            if (scene.Channels.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("No channels. Click '+ Add Channel' to create one.");
            }

            const float kDelW = ImGui::GetFrameHeight();

            for (int ci = 0; ci < (int)scene.Channels.size(); )
            {
                auto& ch = scene.Channels[ci];
                ImGui::PushID(ci);

                char hdrLabel[160];
                snprintf(hdrLabel, sizeof(hdrLabel), "%d.  %s##chsec%d",
                         ci + 1,
                         ch.Name.empty() ? "(unnamed)" : ch.Name.c_str(),
                         ci);

                float availW       = ImGui::GetContentRegionAvail().x;
                ImVec2 hdrCursor   = ImGui::GetCursorPos();

                bool open = ImGui::CollapsingHeader(hdrLabel,
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

                ImVec2 afterHdr = ImGui::GetCursorPos();

                // Delete button overlaid on right of header row
                ImGui::SetCursorPos(ImVec2(hdrCursor.x + availW - kDelW - 2.0f, hdrCursor.y));
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.14f, 0.14f, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.26f, 0.26f, 1.00f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.48f, 0.10f, 0.10f, 1.00f));
                bool doDelete = ImGui::Button("X", ImVec2(kDelW, kDelW));
                ImGui::PopStyleColor(3);
                ImGui::SetCursorPos(afterHdr);

                if (open && !doDelete)
                {
                    ImGui::Indent(8.0f);
                    if (ImGui::BeginTable("##chtbl", 2, ImGuiTableFlags_None))
                    {
                        ImGui::TableSetupColumn("lbl", ImGuiTableColumnFlags_WidthFixed, 55.0f);
                        ImGui::TableSetupColumn("val", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Name");
                        ImGui::TableSetColumnIndex(1);
                        char nameBuf[128];
                        strncpy(nameBuf, ch.Name.c_str(), sizeof(nameBuf) - 1);
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText("##chname", nameBuf, sizeof(nameBuf)))
                            ch.Name = nameBuf;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Type");
                        ImGui::TableSetColumnIndex(1);
                        int typeIdx = (int)ch.Type;
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::Combo("##chtype", &typeIdx, "Boolean\0Float\0String\0"))
                            ch.Type = (ChannelVariableType)typeIdx;

                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Value");
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(-1);
                        if (ch.Type == ChannelVariableType::Boolean)
                            ImGui::Checkbox("##chbool", &ch.BoolValue);
                        else if (ch.Type == ChannelVariableType::Float)
                            ImGui::DragFloat("##chfloat", &ch.FloatValue, 0.05f);
                        else
                        {
                            char strBuf[256];
                            strncpy(strBuf, ch.StringValue.c_str(), sizeof(strBuf) - 1);
                            strBuf[sizeof(strBuf) - 1] = '\0';
                            if (ImGui::InputText("##chstr", strBuf, sizeof(strBuf)))
                                ch.StringValue = strBuf;
                        }

                        ImGui::EndTable();
                    }
                    ImGui::Unindent(8.0f);
                    ImGui::Spacing();
                }

                ImGui::PopID();

                if (doDelete)
                    scene.Channels.erase(scene.Channels.begin() + ci);
                else
                    ++ci;
            }
        }

        ImGui::EndChild();
        ImGui::End();
    }

    // ---------------------------------------------------------------
    // Viewport drop target (invisible overlay for drag-to-viewport)
    // Only active when dragging a prefab or mesh asset
    // ---------------------------------------------------------------
    if (m_ViewportTab == 0)  // Only in scene/editor camera view
    {
        const ImGuiPayload* peeked = ImGui::GetDragDropPayload();
        bool hasDrag = peeked && (strcmp(peeked->DataType, "PREFAB_IDX") == 0 ||
                                   strcmp(peeked->DataType, "MESH_ASSET") == 0);
        if (hasDrag)
        {
            const float vx = (float)m_LeftPanelWidth;
            const float vy = (float)topStackH;
            const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
            const float vh = (float)(winH - k_AssetH - topStackH);

            ImGui::SetNextWindowPos(ImVec2(vx, vy));
            ImGui::SetNextWindowSize(ImVec2(vw, vh));
            ImGui::SetNextWindowBgAlpha(0.0f); // fully transparent
            ImGui::Begin("##viewportdrop", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBringToFrontOnFocus);

            // Show ghost preview text at cursor
            ImVec2 mpos = ImGui::GetIO().MousePos;
            if (mpos.x >= vx && mpos.x <= vx + vw && mpos.y >= vy && mpos.y <= vy + vh)
            {
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                dl->AddRectFilled(ImVec2(mpos.x - 20, mpos.y - 20),
                                  ImVec2(mpos.x + 20, mpos.y + 20),
                                  IM_COL32(100, 200, 255, 80), 4.0f);
                dl->AddRect(ImVec2(mpos.x - 20, mpos.y - 20),
                            ImVec2(mpos.x + 20, mpos.y + 20),
                            IM_COL32(100, 200, 255, 180), 4.0f, 0, 1.5f);
                dl->AddText(ImVec2(mpos.x - 8, mpos.y - 6), IM_COL32(255, 255, 255, 200), "+");
            }

            // Full-window invisible button for drop target
            ImGui::InvisibleButton("##vpdropbtn", ImGui::GetContentRegionAvail());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("PREFAB_IDX"))
                {
                    int pi2 = *(const int*)p->Data;
                    if (pi2 >= 0 && pi2 < (int)scene.Prefabs.size())
                    {
                        // Instantiate prefab at cursor world position
                        const PrefabAsset& pref = scene.Prefabs[pi2];
                        if (!pref.Nodes.empty())
                        {
                            // Compute world position from mouse
                            glm::vec3 spawnPos = m_ViewportDropPos;

                            std::vector<int> nodeId(pref.Nodes.size(), -1);
                            for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni) {
                                const PrefabEntityData& node = pref.Nodes[ni];
                                Entity e = scene.CreateEntity(node.Name);
                                int id = (int)e.GetID();
                                nodeId[ni] = id;
                                scene.Transforms[id] = node.Transform;
                                if (ni == 0) scene.Transforms[id].Position = spawnPos;
                                if (!node.MeshType.empty()) {
                                    Mesh* mesh = nullptr;
                                    const std::string& mt = node.MeshType;
                                    if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
                                    else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
                                    else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
                                    else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
                                    else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
                                    else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
                                    else if (mt.size() > 4 && mt.substr(0,4) == "obj:") mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
                                    scene.MeshRenderers[id].MeshPtr = mesh;
                                    scene.MeshRenderers[id].MeshType = mt;
                                }
                                if (!node.MaterialName.empty()) {
                                    for (int mi2 = 0; mi2 < (int)scene.Materials.size(); ++mi2)
                                        if (scene.Materials[mi2].Name == node.MaterialName) { scene.EntityMaterial[id] = mi2; break; }
                                }
                                if (node.HasLight)  scene.Lights[id] = node.Light;
                                if (node.HasCamera) scene.GameCameras[id] = node.Camera;
                                while ((int)scene.EntityIsPrefabInstance.size() <= id) scene.EntityIsPrefabInstance.push_back(false);
                                scene.EntityIsPrefabInstance[id] = true;
                                while ((int)scene.EntityPrefabSource.size() <= id) scene.EntityPrefabSource.push_back(-1);
                                scene.EntityPrefabSource[id] = pi2;
                            }
                            for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni) {
                                int pidx = pref.Nodes[ni].ParentIdx;
                                if (pidx >= 0 && pidx < (int)nodeId.size()) scene.SetEntityParent(nodeId[ni], nodeId[pidx]);
                            }
                            selectedEntity = nodeId[0];
                        }
                    }
                }
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MESH_ASSET"))
                {
                    int oi2 = *(const int*)p->Data;
                    if (oi2 >= 0 && oi2 < (int)scene.MeshAssets.size())
                    {
                        const std::string& oPath = scene.MeshAssets[oi2];
                        size_t sl = oPath.find_last_of("/\\");
                        std::string oName = (sl != std::string::npos) ? oPath.substr(sl+1) : oPath;
                        if (oName.size() > 4 && oName.substr(oName.size()-4) == ".obj")
                            oName = oName.substr(0, oName.size()-4);

                        Mesh loaded = Mesh::LoadOBJ(oPath);
                        Entity e = scene.CreateEntity(oName);
                        int id = (int)e.GetID();
                        scene.MeshRenderers[id].MeshPtr  = new Mesh(std::move(loaded));
                        scene.MeshRenderers[id].MeshType = "obj:" + oPath;
                        scene.Transforms[id].Position = m_ViewportDropPos;
                        selectedEntity = id;
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::End();
        }
    }

    // ---------------------------------------------------------------
    // Procedural Maze overview (tab 4) â€” viewport-area panel
    // ---------------------------------------------------------------
    if (m_ViewportTab == 4 && m_MazeWindowOpen)
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);

        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        // (theme-managed bg)
        ImGui::Begin("##maze_overview", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

        // ---- Room Sets list ----
        ImGui::TextColored(ImVec4(0.75f, 0.82f, 1.0f, 1.0f), "Room Sets");
        ImGui::SameLine();
        float addBtnMazeW = 120.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - addBtnMazeW);
        if (ImGui::Button("+ Add Room Set", ImVec2(addBtnMazeW, 0)))
        {
            RoomSet rs;
            rs.Name = "Room Set " + std::to_string(scene.RoomSets.size() + 1);
            scene.RoomSets.push_back(rs);
        }
        ImGui::Separator();

        if (scene.RoomSets.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No room sets. Click '+ Add Room Set' to create one.");
        }

        for (int si = 0; si < (int)scene.RoomSets.size(); )
        {
            auto& rs = scene.RoomSets[si];
            ImGui::PushID(si);

            char setLabel[128];
            snprintf(setLabel, sizeof(setLabel), "%s##rset%d", rs.Name.c_str(), si);

            float kDelBtn = ImGui::GetFrameHeight();
            float availMW = ImGui::GetContentRegionAvail().x;
            ImVec2 hdrCursorM = ImGui::GetCursorPos();

            bool setOpen = ImGui::CollapsingHeader(setLabel,
                ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

            ImVec2 afterHdrM = ImGui::GetCursorPos();

            ImGui::SetCursorPos(ImVec2(hdrCursorM.x + availMW - kDelBtn - 2.0f, hdrCursorM.y));
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.14f, 0.14f, 0.80f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.26f, 0.26f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.48f, 0.10f, 0.10f, 1.00f));
            bool doDeleteSet = ImGui::Button("X", ImVec2(kDelBtn, kDelBtn));
            ImGui::PopStyleColor(3);
            ImGui::SetCursorPos(afterHdrM);

            if (setOpen && !doDeleteSet)
            {
                ImGui::Indent(8.0f);

                char setNameBuf[128];
                strncpy(setNameBuf, rs.Name.c_str(), sizeof(setNameBuf) - 1);
                setNameBuf[sizeof(setNameBuf) - 1] = '\0';
                ImGui::SetNextItemWidth(200);
                if (ImGui::InputText("Name##setname", setNameBuf, sizeof(setNameBuf)))
                    rs.Name = setNameBuf;

                ImGui::Spacing();

                if (ImGui::Button("+ Add Room"))
                {
                    RoomTemplate rt;
                    rt.Name = "Room " + std::to_string(rs.Rooms.size() + 1);
                    RoomBlock b; b.X = 0; b.Y = 0; b.Z = 0;
                    rt.Blocks.push_back(b);
                    rs.Rooms.push_back(rt);
                }

                for (int ri = 0; ri < (int)rs.Rooms.size(); )
                {
                    auto& room = rs.Rooms[ri];
                    ImGui::PushID(ri);

                    ImGui::Separator();
                    char roomNameBuf[128];
                    strncpy(roomNameBuf, room.Name.c_str(), sizeof(roomNameBuf) - 1);
                    roomNameBuf[sizeof(roomNameBuf) - 1] = '\0';
                    ImGui::SetNextItemWidth(140);
                    if (ImGui::InputText("##rname", roomNameBuf, sizeof(roomNameBuf)))
                        room.Name = roomNameBuf;

                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80);
                    ImGui::DragFloat("Rarity", &room.Rarity, 0.05f, 0.01f, 100.0f, "%.2f");

                    ImGui::SameLine();
                    if (ImGui::Button("Edit"))
                    {
                        m_MazeRoomEditorOpen = true;
                        m_EditingRoomSetIdx  = si;
                        m_EditingRoomIdx     = ri;
                        m_RequestViewportTab = 5;
                    }

                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d blocks, %d doors)",
                        (int)room.Blocks.size(), (int)room.Doors.size());

                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.60f, 0.14f, 0.14f, 0.80f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.26f, 0.26f, 1.00f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.48f, 0.10f, 0.10f, 1.00f));
                    bool doDelRoom = ImGui::Button("X##delroom");
                    ImGui::PopStyleColor(3);

                    ImGui::PopID();

                    if (doDelRoom)
                    {
                        if (m_EditingRoomSetIdx == si && m_EditingRoomIdx == ri)
                        {
                            m_MazeRoomEditorOpen = false;
                            m_EditingRoomSetIdx = -1;
                            m_EditingRoomIdx = -1;
                            if (m_ViewportTab == 5) m_ViewportTab = 4;
                        }
                        rs.Rooms.erase(rs.Rooms.begin() + ri);
                    }
                    else
                        ++ri;
                }

                ImGui::Unindent(8.0f);
                ImGui::Spacing();
            }

            ImGui::PopID();

            if (doDeleteSet)
            {
                if (m_EditingRoomSetIdx == si)
                {
                    m_MazeRoomEditorOpen = false;
                    m_EditingRoomSetIdx = -1;
                    m_EditingRoomIdx = -1;
                    if (m_ViewportTab == 5) m_ViewportTab = 4;
                }
                for (auto& mg : scene.MazeGenerators)
                {
                    if (mg.RoomSetIndex == si) mg.RoomSetIndex = -1;
                    else if (mg.RoomSetIndex > si) mg.RoomSetIndex--;
                }
                scene.RoomSets.erase(scene.RoomSets.begin() + si);
            }
            else
                ++si;
        }

        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("Place Generator in Scene", ImVec2(-1, 30)))
        {
            Entity e = scene.CreateEntity("MazeGenerator");
            int id = (int)e.GetID();
            scene.MazeGenerators[id].Active  = true;
            scene.MazeGenerators[id].Enabled = true;
            selectedEntity = id;
        }
        ImGui::TextDisabled("Creates an empty entity with a Maze Generator component.");

        ImGui::End();
    }

    // ---------------------------------------------------------------
    // Realtime Graphics viewport panel (tab 6)
    // ---------------------------------------------------------------
    if (m_ViewportTab == 6 && m_RealtimeWindowOpen)
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);

        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        ImGui::Begin("##realtime_view", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Realtime Graphics");
        ImGui::Separator();
        ImGui::Spacing();

        auto& rt = scene.Realtime;
        ImGui::Checkbox("Enable Shadows", &rt.EnableShadows);
        ImGui::Checkbox("Frustum Culling", &rt.FrustumCulling);
        ImGui::Checkbox("Distance Culling", &rt.DistanceCulling);
        if (rt.DistanceCulling)
            ImGui::SliderFloat("Max Draw Distance", &rt.MaxDrawDistance, 10.0f, 2000.0f, "%.0f");
        ImGui::Checkbox("Use Fog", &rt.UseFog);
        ImGui::Checkbox("Use Sky Ambient", &rt.UseSkyAmbient);
        ImGui::SliderInt("Max Realtime Lights", &rt.MaxRealtimeLights, 1, 8);
        ImGui::SliderInt("Shadow 2D Size", &rt.ShadowMapSize2D, 256, 4096);
        ImGui::SliderInt("Shadow Cube Size", &rt.ShadowMapSizeCube, 128, 2048);
        ImGui::SliderFloat("Dir Shadow Distance", &rt.DirectionalShadowDistance, 5.0f, 300.0f, "%.1f");
        ImGui::SliderFloat("Dir Shadow Ortho", &rt.DirectionalShadowOrtho, 5.0f, 200.0f, "%.1f");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Post-process");
        ImGui::Spacing();
        ImGui::Checkbox("SSAO Lite", &scene.PostProcess.SSAOEnabled);
        ImGui::SliderFloat("SSAO Intensity", &scene.PostProcess.SSAOIntensity, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("SSAO Radius", &scene.PostProcess.SSAORadius, 0.5f, 8.0f, "%.1f");
        // --- External LUT ---
        ImGui::Checkbox("Use External LUT", &scene.PostProcess.LUTEnabled);
        if (scene.PostProcess.LUTEnabled)
        {
            static char s_LutPathBuf[512] = {};
            static Scene* s_PrevLutScene = nullptr;
            if (&scene != s_PrevLutScene)
            {
                s_PrevLutScene = &scene;
                strncpy(s_LutPathBuf, scene.PostProcess.LUTPath.c_str(), sizeof(s_LutPathBuf)-1);
                s_LutPathBuf[sizeof(s_LutPathBuf)-1] = '\0';
            }
            ImGui::TextDisabled("LUT Texture (drag from Assets or type path)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##lut_path", "assets/lut.png", s_LutPathBuf, sizeof(s_LutPathBuf)))
                scene.PostProcess.LUTPath = s_LutPathBuf;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    std::string path((const char*)p->Data, p->DataSize);
                    strncpy(s_LutPathBuf, path.c_str(), sizeof(s_LutPathBuf)-1);
                    scene.PostProcess.LUTPath = s_LutPathBuf;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SliderFloat("LUT Strength", &scene.PostProcess.LUTStrength, 0.0f, 1.0f, "%.2f");
        }

        // --- External Post Shader ---
        ImGui::Checkbox("Use External Post Shader", &scene.PostProcess.UseExternalPostShader);
        if (scene.PostProcess.UseExternalPostShader)
        {
            static char s_PostPathBuf[512] = {};
            static Scene* s_PrevPostScene = nullptr;
            if (&scene != s_PrevPostScene)
            {
                s_PrevPostScene = &scene;
                strncpy(s_PostPathBuf, scene.PostProcess.ExternalPostPath.c_str(), sizeof(s_PostPathBuf)-1);
                s_PostPathBuf[sizeof(s_PostPathBuf)-1] = '\0';
            }
            ImGui::TextDisabled("Fragment shader file (drag from Assets or type path)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##post_path", "assets/shaders/post.frag", s_PostPathBuf, sizeof(s_PostPathBuf)))
                scene.PostProcess.ExternalPostPath = s_PostPathBuf;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    std::string path((const char*)p->Data, p->DataSize);
                    strncpy(s_PostPathBuf, path.c_str(), sizeof(s_PostPathBuf)-1);
                    scene.PostProcess.ExternalPostPath = s_PostPathBuf;
                }
                ImGui::EndDragDropTarget();
            }
        }

        // --- External Color Config ---
        ImGui::Checkbox("Use External Color Config", &scene.PostProcess.UseExternalColorCfg);
        if (scene.PostProcess.UseExternalColorCfg)
        {
            static char s_ColorPathBuf[512] = {};
            static Scene* s_PrevColorScene = nullptr;
            if (&scene != s_PrevColorScene)
            {
                s_PrevColorScene = &scene;
                strncpy(s_ColorPathBuf, scene.PostProcess.ExternalColorCfgPath.c_str(), sizeof(s_ColorPathBuf)-1);
                s_ColorPathBuf[sizeof(s_ColorPathBuf)-1] = '\0';
            }
            ImGui::TextDisabled("Color config .ini file (drag from Assets or type path)");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##color_path", "assets/color.ini", s_ColorPathBuf, sizeof(s_ColorPathBuf)))
                scene.PostProcess.ExternalColorCfgPath = s_ColorPathBuf;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
                {
                    std::string path((const char*)p->Data, p->DataSize);
                    strncpy(s_ColorPathBuf, path.c_str(), sizeof(s_ColorPathBuf)-1);
                    scene.PostProcess.ExternalColorCfgPath = s_ColorPathBuf;
                }
                ImGui::EndDragDropTarget();
            }
        }
        ImGui::End();
    }

    // ---------------------------------------------------------------
    // Baked Lighting viewport panel (tab 7)
    // ---------------------------------------------------------------
    if (m_ViewportTab == 7 && m_BakedLightingWindowOpen)
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);

        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        ImGui::Begin("##bakedlighting_view", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

        ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Baked Lighting");
        ImGui::Separator();
        ImGui::Spacing();

        float sliderW = std::min(vw * 0.5f, 300.0f);

        // ---- Runtime lightmap intensity (real-time, no rebake needed) ----
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Lightmap Runtime");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("Lightmap Intensity##lm", &scene.LightmapIntensity, 0.0f, 4.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Runtime multiplier for baked GI.\n0 = disable baked contribution, 1 = default, >1 = stronger bake.\nAdjust without re-baking.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Auto Intensity Behavior");
        ImGui::Checkbox("Auto Apply From Bake Peak", &scene.BakedLighting.AutoApplyLightmapIntensity);
        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("Auto Multiplier", &scene.BakedLighting.AutoIntensityMultiplier, 0.1f, 4.0f, "%.2f");
        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("Max Auto Intensity", &scene.BakedLighting.MaxAutoIntensity, 0.1f, 8.0f, "%.2f");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Bake Parameters ----
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Bake Parameters");
        ImGui::TextDisabled("(require re-bake to take effect)");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt("Resolution##lm", &m_BakeParams.resolution, 32, 2048);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Output lightmap texture size. Higher = better quality, slower bake.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt("AO Samples##lm", &m_BakeParams.aoSamples, 4, 512);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Hemisphere samples for ambient occlusion. More = less noise.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt("Indirect Samples##lm", &m_BakeParams.indirectSamples, 8, 512);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("GI hemisphere samples per texel. More = smoother indirect lighting, slower bake.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt("Bounce Count##lm", &m_BakeParams.bounceCount, 1, 4);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Simplified indirect bounce count. 2 is usually enough for indoor scenes.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("AO Radius##lm", &m_BakeParams.aoRadius, 0.5f, 20.0f, "%.2f units");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Max occlusion search distance.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("Ambient Level##lm", &m_BakeParams.ambientLevel, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Base brightness in occluded/shadowed areas.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderFloat("Direct Scale##lm", &m_BakeParams.directScale, 0.0f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Multiplier for direct light contribution in the bake.");

        ImGui::SetNextItemWidth(sliderW);
        ImGui::SliderInt("Denoise Radius##lm", &m_BakeParams.denoiseRadius, 0, 3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Post-bake blur radius in texels. 1-2 reduces noise and banding.");

        ImGui::Spacing();
        ImGui::TextUnformatted("Bake Presets");
        if (ImGui::Button("Draft##bake_preset", ImVec2(110, 0)))
        {
            m_BakeParams.resolution = 256;
            m_BakeParams.aoSamples = 16;
            m_BakeParams.indirectSamples = 24;
            m_BakeParams.bounceCount = 1;
            m_BakeParams.denoiseRadius = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Balanced##bake_preset", ImVec2(110, 0)))
        {
            m_BakeParams.resolution = 512;
            m_BakeParams.aoSamples = 64;
            m_BakeParams.indirectSamples = 96;
            m_BakeParams.bounceCount = 2;
            m_BakeParams.denoiseRadius = 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Production##bake_preset", ImVec2(110, 0)))
        {
            m_BakeParams.resolution = 1024;
            m_BakeParams.aoSamples = 128;
            m_BakeParams.indirectSamples = 192;
            m_BakeParams.bounceCount = 3;
            m_BakeParams.denoiseRadius = 2;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ---- Bake button ----
        bool baking = m_BakeInProgress;
        if (baking) ImGui::BeginDisabled();
        if (ImGui::Button("Bake Scene Lighting##lm", ImVec2(200, 36)))
        {
            m_PendingBakeLighting = true;
            m_BakeStatus.clear();
            m_BakeProgress = 0.0f;
            m_BakeStage = "Preparing bake";
        }
        if (baking) ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && !baking)
            ImGui::SetTooltip("UV2 atlas bake (no top-down projection).\nRuns asynchronously and applies to static receivers.");

        if (m_BakeInProgress)
        {
            ImGui::Spacing();
            ImGui::Text("%s", m_BakeStage.empty() ? "Baking..." : m_BakeStage.c_str());
            ImGui::ProgressBar(m_BakeProgress, ImVec2(std::min(vw * 0.55f, 340.0f), 0.0f));
        }

        // ---- Clear Bake button ----
        ImGui::SameLine();
        bool hasBake = !m_SceneLightmapPath.empty();
        if (!hasBake) ImGui::BeginDisabled();
        if (ImGui::Button("Clear Bake##lm", ImVec2(120, 36)) && hasBake)
        {
            m_PendingClearSceneLightmap = true;
            m_SceneLightmapPath.clear();
            m_BakeStatus.clear();
        }
        if (!hasBake) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove the baked lightmap from all scene entities.");

        // ---- Status line ----
        if (!m_BakeStatus.empty())
        {
            ImGui::Spacing();
            bool ok = (m_BakeStatus.rfind("Error", 0) != 0);
            if (ok)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", m_BakeStatus.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_BakeStatus.c_str());
        }

        // ---- Lightmap preview ----
        if (!m_SceneLightmapPath.empty())
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            std::string disp = m_SceneLightmapPath;
            if (disp.size() > 50) disp = "..." + disp.substr(disp.size() - 47);
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", disp.c_str());

            unsigned int lmId = LightmapManager::Instance().GetCachedID(m_SceneLightmapPath);
            if (lmId == 0) lmId = LightmapManager::Instance().Load(m_SceneLightmapPath);
            if (lmId != 0)
            {
                float previewSz = std::min(vw * 0.35f, 256.0f);
                ImGui::Image((ImTextureID)(intptr_t)lmId,
                             ImVec2(previewSz, previewSz),
                             ImVec2(0, 1), ImVec2(1, 0));
            }
        }

        ImGui::End();
    }

    // Animation Controller viewport panel (tab 8)
    if (m_ViewportTab == 8 && m_AnimControllerWindowOpen)
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);
        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        ImGui::Begin("##anim_controller_view", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);
        ImGui::TextColored(ImVec4(0.85f, 0.80f, 1.0f, 1.0f), "Animation Controller");
        ImGui::Separator();
        ImGui::Spacing();
        int currentIdx = selectedEntity;
        if (currentIdx < 0 || currentIdx >= (int)scene.AnimationControllers.size())
            ImGui::TextDisabled("Select an entity to edit its controller.");
        else
        {
            auto& ac = scene.AnimationControllers[currentIdx];
            ImGui::Checkbox("Enabled", &ac.Enabled);
            ImGui::Checkbox("Auto Start", &ac.AutoStart);
            ImGui::InputInt("Default State", &ac.DefaultState);
            ImGui::Separator();
            ImGui::TextUnformatted("States");
            if (ImGui::BeginTable("##ac_states", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("AutoPlay", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                for (int si = 0; si < (int)ac.States.size(); ++si)
                {
                    auto& st = ac.States[si];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text("%d", si);
                    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
                    char nb[128]; strncpy(nb, st.Name.c_str(), 127); nb[127]='\0';
                    if (ImGui::InputText(("##stn"+std::to_string(si)).c_str(), nb, sizeof(nb)))
                        st.Name = nb;
                    ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1);
                    ImGui::InputFloat(("##std"+std::to_string(si)).c_str(), &st.Duration, 0.1f, 1.0f, "%.2f");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Checkbox(("##sta"+std::to_string(si)).c_str(), &st.AutoPlay);
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("+ Add State", ImVec2(120,0)))
            {
                AnimationStateData ns;
                ns.Name = "State";
                ac.States.push_back(ns);
            }
            ImGui::Separator();
            ImGui::TextUnformatted("Transitions");
            if (ImGui::BeginTable("##ac_trans", 6, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("To", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Cond", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Thres", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Exit", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                for (int ti = 0; ti < (int)ac.Transitions.size(); ++ti)
                {
                    auto& tr = ac.Transitions[ti];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt(("##trf"+std::to_string(ti)).c_str(), &tr.FromState);
                    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt(("##trt"+std::to_string(ti)).c_str(), &tr.ToState);
                    ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1);
                    ImGui::InputInt(("##trc"+std::to_string(ti)).c_str(), &tr.Channel);
                    ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1);
                    int cond = (int)tr.Condition; ImGui::InputInt(("##trco"+std::to_string(ti)).c_str(), &cond); tr.Condition = (AnimationConditionOp)cond;
                    ImGui::TableSetColumnIndex(4); ImGui::SetNextItemWidth(-1);
                    ImGui::InputFloat(("##trth"+std::to_string(ti)).c_str(), &tr.Threshold, 0.1f, 1.0f, "%.2f");
                    ImGui::TableSetColumnIndex(5); ImGui::SetNextItemWidth(-1);
                    ImGui::InputFloat(("##trex"+std::to_string(ti)).c_str(), &tr.ExitTime, 0.1f, 1.0f, "%.2f");
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("+ Add Transition", ImVec2(140,0)))
            {
                AnimationTransitionData nt;
                nt.FromState = 0; nt.ToState=0; nt.Channel=-1; nt.Condition=AnimationConditionOp::BoolTrue;
                ac.Transitions.push_back(nt);
            }
        }
        ImGui::End();
    }
    if (m_MultiplayerTestWindowOpen)
    {
        ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_FirstUseEver);
        ImGui::Begin("Multiplayer Local Test", &m_MultiplayerTestWindowOpen);

        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1.0f), "Quick Launch");
        ImGui::Separator();
        ImGui::TextWrapped("Opens two game windows side-by-side (Host + Client) connected on localhost.");
        ImGui::Spacing();

        ImGui::InputText("Host Nick", m_MpHostNickBuf, sizeof(m_MpHostNickBuf));
        ImGui::InputText("Client Nick", m_MpClientNickBuf, sizeof(m_MpClientNickBuf));
        ImGui::InputInt("Port", &m_MpPort);
        if (m_MpPort < 1) m_MpPort = 1;
        if (m_MpPort > 65535) m_MpPort = 65535;
        ImGui::Spacing();

        float bw = ImGui::GetContentRegionAvail().x;
        if (ImGui::Button("Launch Host + Client", ImVec2(bw, 36)))
            m_PendingLaunchMpPair = true;
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Advanced");
        ImGui::Separator();
        ImGui::InputText("Server", m_MpServerBuf, sizeof(m_MpServerBuf));
        ImGui::Spacing();

        MultiplayerManagerComponent* mm = nullptr;
        for (auto& it : scene.MultiplayerManagers)
            if (it.Active) { mm = &it; break; }

        if (ImGui::Button("Launch Host Only"))
        {
            if (mm)
            {
                mm->Mode = MultiplayerMode::Host;
                mm->Port = m_MpPort;
                mm->BindAddress = "0.0.0.0";
                mm->RequestStartHost = true;
                mm->Running = true;
            }
            else m_PendingLaunchMpHost = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Launch Client Only"))
        {
            if (mm)
            {
                mm->Mode = MultiplayerMode::Client;
                mm->Port = m_MpPort;
                mm->ServerAddress = m_MpServerBuf;
                mm->RequestStartClient = true;
                mm->Running = true;
            }
            else m_PendingLaunchMpClient = true;
        }
        if (mm)
        {
            ImGui::SameLine();
            if (ImGui::Button("Stop")) mm->RequestStop = true;
        }
        ImGui::End();
    }

    if (m_IsPlaying)
    {
        MultiplayerManagerComponent* mm = nullptr;
        for (auto& it : scene.MultiplayerManagers)
            if (it.Active) { mm = &it; break; }
        if (mm)
        {
            ImGui::SetNextWindowBgAlpha(0.55f);
            ImGui::SetNextWindowPos(ImVec2((float)m_LeftPanelWidth + 10.0f, (float)topStackH + 10.0f), ImGuiCond_Always);
            ImGui::Begin("##mp_hud_overlay", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
            ImGui::Text("MP: %s", mm->Mode == MultiplayerMode::Host ? "Host" : "Client");
            ImGui::Text("State: %s", mm->RuntimeState.c_str());
            ImGui::Text("Players: %d", mm->ConnectedPlayers);
            ImGui::Text("Ping: %.1f ms", mm->EstimatedPingMs);
            ImGui::End();
        }
    }

    // ---------------------------------------------------------------
    // Room Editor 3D viewport toolbar (tab 5) â€” block / door controls
    // The 3D scene + wireframes are rendered by Application.
    // This horizontal toolbar sits at the bottom of the viewport.
    // ---------------------------------------------------------------
    if (m_ViewportTab == 5 && m_MazeRoomEditorOpen &&
        m_EditingRoomSetIdx >= 0 && m_EditingRoomSetIdx < (int)scene.RoomSets.size() &&
        m_EditingRoomIdx >= 0 && m_EditingRoomIdx < (int)scene.RoomSets[m_EditingRoomSetIdx].Rooms.size())
    {
        const float vx = (float)m_LeftPanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_LeftPanelWidth - m_RightPanelWidth);
        const float vh = (float)(winH - k_AssetH - topStackH);

        RoomTemplate& editRoom = scene.RoomSets[m_EditingRoomSetIdx].Rooms[m_EditingRoomIdx];

        // Compact horizontal toolbar at top of viewport
        float barH = 38.0f;
        ImGui::SetNextWindowPos(ImVec2(vx + 4.0f, vy + 4.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw - 8.0f, barH), ImGuiCond_Always);
        // (theme-managed bg)
        ImGui::Begin("##roombar", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoScrollbar);

        // ---- Tool buttons: Block / Door ----
        auto pushToolStyle = [](bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.50f, 0.90f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.62f, 1.00f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
            }
        };
        auto popToolStyle = [](bool /*active*/) { ImGui::PopStyleColor(2); };

        bool blockActive = !m_MazeDoorMode;
        pushToolStyle(blockActive);
        if (ImGui::Button("Block")) m_MazeDoorMode = false;
        popToolStyle(blockActive);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("LMB=add block  Shift+LMB=erase  Ctrl=extend all  Ctrl+Shift=retract all");

        ImGui::SameLine();
        pushToolStyle(m_MazeDoorMode);
        if (ImGui::Button("Door")) m_MazeDoorMode = true;
        popToolStyle(m_MazeDoorMode);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("LMB=toggle door on block face");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ---- Show/hide block wireframes ----
        pushToolStyle(m_RoomShowBlocks);
        if (ImGui::Button(m_RoomShowBlocks ? "Blocks: ON" : "Blocks: OFF"))
            m_RoomShowBlocks = !m_RoomShowBlocks;
        popToolStyle(m_RoomShowBlocks);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle block wireframe + face arrows\n(hide to edit interior props freely)");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ---- Y-Layer control ----
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Y:");
        ImGui::SameLine();
        if (ImGui::ArrowButton("##yd", ImGuiDir_Down)) m_RoomEditorLayer--;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(44.0f);
        ImGui::DragInt("##yl", &m_RoomEditorLayer, 0.15f, -20, 20);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##yu", ImGuiDir_Up)) m_RoomEditorLayer++;

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        ImGui::TextDisabled("Shift=erase | Ctrl=extend all | Arrows=expand edge");

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        // ---- Bake Lighting button ----
        bool baking = m_PendingBakeLighting;
        if (baking) ImGui::BeginDisabled();
        if (ImGui::Button("Bake Lighting"))
            m_PendingBakeLighting = true;
        if (baking) ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("CPU-bake ambient occlusion + direct light lightmap for this room");

        // Show bake status badge next to button
        if (!m_BakeStatus.empty()) {
            ImGui::SameLine();
            bool ok = (m_BakeStatus.rfind("Error", 0) != 0);
            if (ok)
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "%s", m_BakeStatus.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", m_BakeStatus.c_str());
        }

        ImGui::SameLine(ImGui::GetWindowWidth() - 148.0f);
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Blocks:%d Doors:%d",
            (int)editRoom.Blocks.size(), (int)editRoom.Doors.size());

        ImGui::End();

        // ---- 3D viewport click handling: add / remove blocks via raycasting ----
        // Only process if ImGui doesn't want the mouse and we're not rotating the camera
        // Skip if an arrow was handled this frame (prevents double-placement)
        bool blockClick = m_BlockNextRoomClick;
        m_BlockNextRoomClick = false;
        if (!blockClick && m_RoomShowBlocks &&
            !ImGui::GetIO().WantCaptureMouse &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImVec2 mpos = ImGui::GetIO().MousePos;
            // Check mouse is within viewport
            if (mpos.x >= vx && mpos.x <= vx + vw &&
                mpos.y >= vy + barH && mpos.y <= vy + vh)
            {
                // Convert screen coords to NDC for raycast (actual ray computed in Application)
                float ndcX = ((mpos.x - vx) / vw) * 2.0f - 1.0f;
                float ndcY = 1.0f - ((mpos.y - vy) / vh) * 2.0f;

                m_PendingRoomClick    = true;
                m_PendingRoomClickNDC = glm::vec2(ndcX, ndcY);
                m_PendingRoomClickY   = m_RoomEditorLayer;
            }
        }
    }

    // ---------------------------------------------------------------
    // Bottom panel: Assets / Console tabs
    // ---------------------------------------------------------------
    ImGui::SetNextWindowPos (ImVec2(0.0f, (float)(winH - k_AssetH)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)k_AssetH),   ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImGui::Begin("##bottompanel", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoScrollbar);

    // Tab bar: Assets | Console
    if (ImGui::BeginTabBar("##bottomtabs"))
    {
        if (ImGui::BeginTabItem("Assets")) { m_BottomTab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Console")) { m_BottomTab = 1; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    if (m_BottomTab == 1)
    {
        // ---- Console ----
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.09f, 1.0f));
        ImGui::BeginChild("##consoleview", ImVec2(-1, -1), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& line : m_ConsoleLogs)
            ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::PopStyleColor(); // ChildBg
        ImGui::End();
        ImGui::PopStyleColor(); // WindowBg
        return ImGui::GetIO().WantCaptureMouse;
    }

    // ---- Assets (m_BottomTab == 0) ----

    // ---- Breadcrumb navigation ----
    // Ensure FolderParents stays in sync with Folders
    while ((int)scene.FolderParents.size() < (int)scene.Folders.size())
        scene.FolderParents.push_back(-1);

    const bool inFolder = (m_CurrentFolder >= 0 && m_CurrentFolder < (int)scene.Folders.size());

    // Universal drop handler: accepts any draggable asset type and assigns it to targetFolder
    auto acceptAssetFolderDrop = [&](int targetFolder) {
        if (auto* pl = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
        { int i=*(const int*)pl->Data; if(i<(int)scene.Materials.size()) scene.Materials[i].Folder=targetFolder; }
        if (auto* pl = ImGui::AcceptDragDropPayload("TEX_MOVE"))
        { int i=*(const int*)pl->Data; if(i<(int)scene.TextureFolders.size()) scene.TextureFolders[i]=targetFolder; }
        if (auto* pl = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
            std::string tp((const char*)pl->Data, pl->DataSize-1);
            auto it = std::find(scene.Textures.begin(), scene.Textures.end(), tp);
            if (it != scene.Textures.end()) {
                int ti2=(int)(it-scene.Textures.begin());
                while ((int)scene.TextureFolders.size() <= ti2) scene.TextureFolders.push_back(-1);
                scene.TextureFolders[ti2] = targetFolder;
            }
        }
        if (auto* pl = ImGui::AcceptDragDropPayload("PREFAB_IDX"))
        { int i=*(const int*)pl->Data; if(i<(int)scene.Prefabs.size()) scene.Prefabs[i].Folder=targetFolder; }
        if (auto* pl = ImGui::AcceptDragDropPayload("MESH_ASSET")) {
            int i=*(const int*)pl->Data;
            while ((int)scene.MeshAssetFolders.size() < (int)scene.MeshAssets.size()) scene.MeshAssetFolders.push_back(-1);
            if (i < (int)scene.MeshAssets.size()) scene.MeshAssetFolders[i] = targetFolder;
        }
        if (auto* pl = ImGui::AcceptDragDropPayload("SCRIPT_ASSET")) {
            std::string sp((const char*)pl->Data, pl->DataSize-1);
            for (int i=0; i<(int)scene.ScriptAssets.size(); ++i)
                if (scene.ScriptAssets[i] == sp) {
                    while ((int)scene.ScriptAssetFolders.size() <= i) scene.ScriptAssetFolders.push_back(-1);
                    scene.ScriptAssetFolders[i] = targetFolder; break;
                }
        }
        if (auto* pl = ImGui::AcceptDragDropPayload("SCENE_PATH")) {
            std::string sp((const char*)pl->Data, pl->DataSize-1);
            m_SceneFileFolders[sp] = targetFolder;
        }
    };

    // Build chain: [grandparent, parent, current] (root â†’ current)
    {
        std::vector<int> chain;
        int cur = m_CurrentFolder;
        while (cur >= 0 && cur < (int)scene.Folders.size()) {
            chain.push_back(cur);
            cur = scene.FolderParents[cur];
        }
        std::reverse(chain.begin(), chain.end());

        // Root segment "Assets"
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.12f));
        if (ImGui::SmallButton("Assets")) m_CurrentFolder = -1;
        ImGui::PopStyleColor(2);
        // Drop on "Assets" = move to root
        if (ImGui::BeginDragDropTarget()) { acceptAssetFolderDrop(-1); ImGui::EndDragDropTarget(); }

        // Intermediate/final breadcrumb segments
        for (int seg : chain) {
            ImGui::SameLine(0,2); ImGui::TextDisabled(">"); ImGui::SameLine(0,2);
            bool isCurrent = (seg == m_CurrentFolder);
            if (isCurrent) {
                ImGui::TextColored(ImVec4(1,0.8f,0.4f,1), "%s", scene.Folders[seg].c_str());
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.12f));
                if (ImGui::SmallButton(scene.Folders[seg].c_str())) m_CurrentFolder = seg;
                ImGui::PopStyleColor(2);
                // Drop on this segment = move item to this folder
                if (ImGui::BeginDragDropTarget()) { acceptAssetFolderDrop(seg); ImGui::EndDragDropTarget(); }
            }
        }
    }
    ImGui::Separator();

    // Scrollable child for all asset icons â€” ensures drop zone covers full area
    ImGui::BeginChild("##assetscroll", ImVec2(0, 0), false);

    const float iconSz = 72.0f;
    const float cellW  = iconSz + 20.0f;
    int   cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
    ImGui::Columns(cols, "##assets", false);

    static char renameBuf[128]  = {};
    static int  renameFolderIdx = -1;
    static int  renameMatIdx    = -1;

    auto drawFolderIconDL = [&](ImVec2 p) {
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({p.x+10,p.y+22},{p.x+iconSz-10,p.y+iconSz-8}, IM_COL32(230,180,50,255), 4.f);
        dl->AddRectFilled({p.x+10,p.y+14},{p.x+iconSz*.5f,p.y+24},      IM_COL32(230,180,50,255), 2.f);
    };
    auto drawMatIconDL = [&](ImVec2 p, const tsu::MaterialAsset& mat) {
        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 mn = {p.x+4, p.y+4};
        ImVec2 mx = {p.x+iconSz-4, p.y+iconSz-4};
        const float r = 6.f;
        if (mat.AlbedoID > 0) {
            // Show actual albedo texture, tinted by material color
            ImVec4 tint = {mat.Color.r, mat.Color.g, mat.Color.b, 1.0f};
            dl->AddImageRounded((ImTextureID)(intptr_t)mat.AlbedoID, mn, mx, {0,1},{1,0},
                IM_COL32((int)(tint.x*255),(int)(tint.y*255),(int)(tint.z*255),255), r);
        } else {
            // Solid color with a subtle gradient
            ImU32 base = IM_COL32((int)(mat.Color.r*255),(int)(mat.Color.g*255),(int)(mat.Color.b*255),255);
            ImU32 dark = IM_COL32((int)(mat.Color.r*140),(int)(mat.Color.g*140),(int)(mat.Color.b*140),255);
            dl->AddRectFilledMultiColor(mn, mx, dark, dark, base, base);
            dl->AddRectFilled(mn, mx, IM_COL32(255,255,255,0), r); // clip corners (workaround)
        }
        // Roughness bar at bottom (white = rough, dark = smooth)
        float rough = mat.Roughness;
        ImVec2 barMn = {mn.x+2, mx.y-6};
        ImVec2 barMx = {mn.x+2+(mx.x-mn.x-4)*rough, mx.y-2};
        dl->AddRectFilled(barMn, {mx.x-2, mx.y-2}, IM_COL32(0,0,0,100));
        dl->AddRectFilled(barMn, barMx, IM_COL32(220,220,220,180));
        // Metallic dot top-right
        if (mat.Metallic > 0.1f) {
            ImU32 mc = IM_COL32(255,(int)(215*mat.Metallic),0,(int)(200*mat.Metallic));
            dl->AddCircleFilled({mx.x-7, mn.y+7}, 4.f, mc);
        }
        dl->AddRect(mn, mx, IM_COL32(255,255,255,60), r);
    };

    // ---- FOLDERS (direct children of m_CurrentFolder) ----
    for (int fi = 0; fi < (int)scene.Folders.size(); ++fi)
    {
        int fp = (fi < (int)scene.FolderParents.size()) ? scene.FolderParents[fi] : -1;
        if (fp != m_CurrentFolder) continue;

        ImGui::PushID(10000 + fi);
        bool sel = (m_SelectedFolder == fi && m_SelectedMaterial < 0);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.6f,1.f,0.2f));
        bool clicked = ImGui::Button("##ficon", {iconSz, iconSz});
        if (sel) ImGui::PopStyleColor();
        ImGui::PopStyleColor(2);
        ImVec2 iconMin = ImGui::GetItemRectMin();

        // Context menu on folder button
        if (ImGui::BeginPopupContextItem("##fctx")) {
            if (ImGui::MenuItem("Rename")) { renameFolderIdx = fi; strncpy(renameBuf,scene.Folders[fi].c_str(),127); renameBuf[127] = '\0'; }
            if (ImGui::MenuItem("Delete")) {
                // Move filhos para o pai desta pasta
                for (auto& m2 : scene.Materials)   if (m2.Folder==fi) m2.Folder=fp; else if(m2.Folder>fi) m2.Folder--;
                for (auto& tf : scene.TextureFolders) if(tf==fi) tf=fp; else if(tf>fi) tf--;
                for (auto& pfp : scene.FolderParents)  if(pfp==fi) pfp=fp; else if(pfp>fi) pfp--;
                scene.Folders.erase(scene.Folders.begin()+fi);
                scene.FolderParents.erase(scene.FolderParents.begin()+fi);
                if (m_SelectedFolder==fi) m_SelectedFolder=-1; else if(m_SelectedFolder>fi) m_SelectedFolder--;
                if (m_CurrentFolder==fi) m_CurrentFolder=fp;   else if(m_CurrentFolder>fi) m_CurrentFolder--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        drawFolderIconDL(iconMin);
        if (clicked) { m_SelectedFolder=fi; m_SelectedMaterial=-1; m_SelectedTexture=-1; m_SelectedPrefab=-1; selectedEntity=-1; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) m_CurrentFolder=fi;

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("FOLDER_IDX", &fi, sizeof(int));
            drawFolderIconDL({4,4}); ImGui::Text("%s", scene.Folders[fi].c_str());
            ImGui::EndDragDropSource();
        }
        // Drop target: move any asset type into this folder
        if (ImGui::BeginDragDropTarget()) { acceptAssetFolderDrop(fi); ImGui::EndDragDropTarget(); }

        // Label / rename
        if (renameFolderIdx==fi) {
            if (!renameBuf[0]) { strncpy(renameBuf, scene.Folders[fi].c_str(), 127); renameBuf[127] = '\0'; }
            ImGui::SetNextItemWidth(iconSz);
            if (ImGui::InputText("##fren", renameBuf, 128,
                ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll))
            { scene.Folders[fi]=renameBuf; renameBuf[0]='\0'; renameFolderIdx=-1; }
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) { renameBuf[0]='\0'; renameFolderIdx=-1; }
        } else {
            std::string lbl=scene.Folders[fi];
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(lbl.c_str()); ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            { renameFolderIdx=fi; renameBuf[0]='\0'; }
        }
        ImGui::NextColumn(); ImGui::PopID();
    }

    // ---- MATERIALS (filtered by current folder, skip hidden) ----
    for (int mi = 0; mi < (int)scene.Materials.size(); ++mi)
    {
        if (scene.Materials[mi].Folder != m_CurrentFolder) continue;
        if (scene.Materials[mi].Hidden) continue;
        ImGui::PushID(20000 + mi);
        bool sel = (m_SelectedMaterial==mi);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.7f,0.3f,0.3f));
        bool clicked = ImGui::Button("##micon", {iconSz, iconSz});
        if (sel) ImGui::PopStyleColor();
        ImGui::PopStyleColor(2);
        ImVec2 iconMin = ImGui::GetItemRectMin();

        // Context menu on material button
        if (ImGui::BeginPopupContextItem("##mctx")) {
            if (ImGui::MenuItem("Rename")) { renameMatIdx=mi; strncpy(renameBuf,scene.Materials[mi].Name.c_str(),127); renameBuf[127] = '\0'; }
            if (inFolder && ImGui::MenuItem("Move to Root")) scene.Materials[mi].Folder=-1;
            if (!scene.Folders.empty() && ImGui::BeginMenu("Move to Folder")) {
                for (int fi2=0; fi2<(int)scene.Folders.size(); ++fi2)
                    if (fi2!=m_CurrentFolder && ImGui::MenuItem(scene.Folders[fi2].c_str())) scene.Materials[mi].Folder=fi2;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete")) {
                scene.Materials.erase(scene.Materials.begin()+mi);
                if (m_SelectedMaterial==mi) m_SelectedMaterial=-1; else if(m_SelectedMaterial>mi) m_SelectedMaterial--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        drawMatIconDL(iconMin, scene.Materials[mi]);
        if (clicked) { m_SelectedMaterial=mi; m_SelectedFolder=-1; m_SelectedTexture=-1; m_SelectedPrefab=-1; selectedEntity=-1; }

        // Drag source MATERIAL_IDX - single payload for folders, entities, and inspector
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("MATERIAL_IDX", &mi, sizeof(int));
            // Preview: cores do material em pequeno quadrado
            ImVec2 cur = ImGui::GetCursorScreenPos();
            ImGui::Dummy({iconSz*0.5f, iconSz*0.5f});
            ImGui::GetWindowDrawList()->AddRectFilled(cur,
                {cur.x+iconSz*0.5f, cur.y+iconSz*0.5f},
                IM_COL32((int)(scene.Materials[mi].Color.r*255),
                         (int)(scene.Materials[mi].Color.g*255),
                         (int)(scene.Materials[mi].Color.b*255), 220), 4.f);
            ImGui::Text("%s", scene.Materials[mi].Name.c_str());
            ImGui::EndDragDropSource();
        }

        // Label / rename
        if (renameMatIdx==mi) {
            if (!renameBuf[0]) { strncpy(renameBuf, scene.Materials[mi].Name.c_str(), 127); renameBuf[127] = '\0'; }
            ImGui::SetNextItemWidth(iconSz);
            if (ImGui::InputText("##mren", renameBuf, 128,
                ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll))
            { scene.Materials[mi].Name=renameBuf; renameBuf[0]='\0'; renameMatIdx=-1; }
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) { renameBuf[0]='\0'; renameMatIdx=-1; }
        } else {
            std::string lbl=scene.Materials[mi].Name;
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(lbl.c_str()); ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            { renameMatIdx=mi; renameBuf[0]='\0'; }
        }
        ImGui::NextColumn(); ImGui::PopID();
    }

    // ---- TEXTURES (filtered by current folder) ----
    for (int ti = 0; ti < (int)scene.Textures.size(); ++ti)
    {
        while ((int)scene.TextureIDs.size()     <= ti) scene.TextureIDs.push_back(0);
        while ((int)scene.TextureFolders.size() <= ti) scene.TextureFolders.push_back(-1);
        if (scene.TextureFolders[ti] != m_CurrentFolder) continue;

        const std::string& fullPath = scene.Textures[ti];
        size_t sep = fullPath.find_last_of("/\\");
        std::string fname = (sep!=std::string::npos) ? fullPath.substr(sep+1) : fullPath;

        ImGui::PushID(30000 + ti);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
        bool tsel = (m_SelectedTexture == ti);
        if (tsel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.5f,0.9f,0.3f));
        bool tclicked = ImGui::Button("##ticon", {iconSz, iconSz});
        if (tsel) ImGui::PopStyleColor();
        ImGui::PopStyleColor(2);
        ImVec2 iconMin = ImGui::GetItemRectMin();

        // Context menu on texture button
        if (ImGui::BeginPopupContextItem("##tctx")) {
            if (inFolder && ImGui::MenuItem("Move to Root")) scene.TextureFolders[ti]=-1;
            if (!scene.Folders.empty() && ImGui::BeginMenu("Move to Folder")) {
                for (int fi2=0; fi2<(int)scene.Folders.size(); ++fi2)
                    if (fi2!=m_CurrentFolder && ImGui::MenuItem(scene.Folders[fi2].c_str())) scene.TextureFolders[ti]=fi2;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete")) {
                if (scene.TextureIDs[ti]>0) glDeleteTextures(1, &scene.TextureIDs[ti]);
                scene.Textures.erase(scene.Textures.begin()+ti);
                scene.TextureIDs.erase(scene.TextureIDs.begin()+ti);
                scene.TextureFolders.erase(scene.TextureFolders.begin()+ti);
                if ((int)scene.TextureSettings.size() > ti)
                    scene.TextureSettings.erase(scene.TextureSettings.begin()+ti);
                if (m_SelectedTexture == ti) m_SelectedTexture = -1;
                else if (m_SelectedTexture > ti) m_SelectedTexture--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        // Thumbnail: UV inverted ({0,1}â†’{1,0}) because texture was loaded with flip=true
        if (scene.TextureIDs[ti] > 0) {
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)scene.TextureIDs[ti],
                {iconMin.x+4, iconMin.y+4}, {iconMin.x+iconSz-4, iconMin.y+iconSz-4},
                {0,1}, {1,0});
        } else {
            auto* dl = ImGui::GetWindowDrawList();
            for (int r=0; r<3; r++) for (int cc=0; cc<3; cc++) {
                float x=iconMin.x+8+cc*18.f, y=iconMin.y+8+r*18.f;
                dl->AddRectFilled({x,y},{x+18,y+18}, (r+cc)%2==0 ? IM_COL32(200,200,200,255) : IM_COL32(120,120,120,255));
            }
            dl->AddRect({iconMin.x+8,iconMin.y+8},{iconMin.x+iconSz-8,iconMin.y+iconSz-8}, IM_COL32(255,255,255,80), 2.f);
        }

        // Drag source: TEXTURE_PATH for inspector/folders
        if (tclicked) { m_SelectedTexture = ti; m_SelectedMaterial = -1; m_SelectedFolder = -1; m_SelectedPrefab = -1; selectedEntity = -1; }
        if (tsel)
            ImGui::GetWindowDrawList()->AddRect(iconMin, {iconMin.x+iconSz-1,iconMin.y+iconSz-1}, IM_COL32(100,150,255,220), 4.f, 0, 2.f);

        // Drag source: TEXTURE_PATH for inspector/folders
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("TEXTURE_PATH", fullPath.c_str(), fullPath.size()+1);
            if (scene.TextureIDs[ti]>0)
                ImGui::Image((ImTextureID)(intptr_t)scene.TextureIDs[ti], {40,40}, {0,1},{1,0});
            else ImGui::Text("Texture");
            ImGui::Text("%s", fname.c_str());
            ImGui::EndDragDropSource();
        }

        { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(fname.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::NextColumn(); ImGui::PopID();
    }

    ImGui::Columns(1);

    // ---- PREFABS ---- (after textures, before background popup)
    {
        // Re-open columns grid for prefabs in current folder
        int pcols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
        ImGui::Columns(pcols, "##prefcols", false);
        static int renamePrefabIdx = -1;
        static char renamePrefabBuf[128] = {};

        auto drawPrefabIconDL = [&](ImVec2 p, bool sel) {
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 mn = {p.x+4, p.y+4}, mx = {p.x+iconSz-4, p.y+iconSz-4};
            const float r = 6.f;
            // Cyan gradient background
            dl->AddRectFilledMultiColor(mn, mx,
                IM_COL32(20,120,140,255), IM_COL32(20,120,140,255),
                IM_COL32(20,80,100,255),  IM_COL32(20,80,100,255));
            // Border
            dl->AddRect(mn, mx, sel ? IM_COL32(100,220,255,255) : IM_COL32(60,180,200,120), r, 0, sel ? 2.f : 1.f);
            // Big "P" letter centred
            float fsz = iconSz * 0.38f;
            float px2 = p.x + iconSz * 0.5f - fsz * 0.28f;
            float py2 = p.y + iconSz * 0.5f - fsz * 0.5f;
            dl->AddText(nullptr, fsz, ImVec2(px2, py2), IM_COL32(180,240,255,220), "P");
        };

        for (int pi = 0; pi < (int)scene.Prefabs.size(); ++pi)
        {
            if (scene.Prefabs[pi].Folder != m_CurrentFolder) continue;
            ImGui::PushID(50000 + pi);
            bool sel = (m_SelectedPrefab == pi);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
            bool clicked = ImGui::Button("##picon", {iconSz, iconSz});
            ImGui::PopStyleColor(2);
            ImVec2 iconMin = ImGui::GetItemRectMin();
            drawPrefabIconDL(iconMin, sel);

            // Context menu
            if (ImGui::BeginPopupContextItem("##pctx")) {
                if (ImGui::MenuItem("Rename")) {
                    renamePrefabIdx = pi;
                    strncpy(renamePrefabBuf, scene.Prefabs[pi].Name.c_str(), 127);
                    renamePrefabBuf[127] = '\0';
                }
                if (ImGui::MenuItem("Instantiate")) {
                    // Spawn all nodes in the prefab into the scene
                    const PrefabAsset& pref = scene.Prefabs[pi];
                    if (!pref.Nodes.empty())
                    {
                        std::vector<int> nodeId(pref.Nodes.size(), -1);
                        for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni) {
                            const PrefabEntityData& node = pref.Nodes[ni];
                            Entity e = scene.CreateEntity(node.Name);
                            int id = (int)e.GetID();
                            nodeId[ni] = id;
                            scene.Transforms[id] = node.Transform;
                            if (!node.MeshType.empty()) {
                                Mesh* mesh = nullptr;
                                const std::string& mt = node.MeshType;
                                if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube("#DDDDDD"));
                                else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere("#DDDDDD"));
                                else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid("#DDDDDD"));
                                else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder("#DDDDDD"));
                                else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule("#DDDDDD"));
                                else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane("#DDDDDD"));
                                else if (mt.size() > 4 && mt.substr(0,4) == "obj:") mesh = new Mesh(Mesh::LoadOBJ(mt.substr(4)));
                                scene.MeshRenderers[id].MeshPtr = mesh;
                                scene.MeshRenderers[id].MeshType = mt;
                            }
                            if (!node.MaterialName.empty()) {
                                for (int mi2 = 0; mi2 < (int)scene.Materials.size(); ++mi2)
                                    if (scene.Materials[mi2].Name == node.MaterialName) { scene.EntityMaterial[id] = mi2; break; }
                            }
                            if (node.HasLight)  scene.Lights[id] = node.Light;
                            if (node.HasCamera) scene.GameCameras[id] = node.Camera;
                            while ((int)scene.EntityIsPrefabInstance.size() <= id) scene.EntityIsPrefabInstance.push_back(false);
                            scene.EntityIsPrefabInstance[id] = true;
                            while ((int)scene.EntityPrefabSource.size() <= id) scene.EntityPrefabSource.push_back(-1);
                            scene.EntityPrefabSource[id] = pi;
                        }
                        for (int ni = 0; ni < (int)pref.Nodes.size(); ++ni) {
                            int pidx = pref.Nodes[ni].ParentIdx;
                            if (pidx >= 0 && pidx < (int)nodeId.size()) scene.SetEntityParent(nodeId[ni], nodeId[pidx]);
                        }
                        selectedEntity = nodeId[0];
                    }
                }
                if (ImGui::MenuItem("Delete")) {
                    scene.Prefabs.erase(scene.Prefabs.begin() + pi);
                    if (m_SelectedPrefab == pi) m_SelectedPrefab = -1;
                    else if (m_SelectedPrefab > pi) m_SelectedPrefab--;
                    for (auto& src : scene.EntityPrefabSource) { if (src == pi) src = -1; else if (src > pi) src--; }
                    if (m_EditingPrefabIdx == pi) { m_PrefabEditorOpen = false; m_EditingPrefabIdx = -1; if (m_ViewportTab == 3) m_ViewportTab = 0; }
                    else if (m_EditingPrefabIdx > pi) m_EditingPrefabIdx--;
                    ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                    return ImGui::GetIO().WantCaptureMouse;
                }
                ImGui::EndPopup();
            }

            if (clicked) { m_SelectedPrefab = pi; m_SelectedMaterial = -1; m_SelectedTexture = -1; m_SelectedFolder = -1; selectedEntity = -1; }

            // Double-click = open prefab editor tab
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                m_PrefabEditorOpen = true;
                m_EditingPrefabIdx = pi;
                m_RequestViewportTab = 3;
            }

            // Drag source: PREFAB_IDX for drag-to-viewport instantiation
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("PREFAB_IDX", &pi, sizeof(int));
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "P");
                ImGui::SameLine();
                ImGui::Text("%s", scene.Prefabs[pi].Name.c_str());
                ImGui::EndDragDropSource();
            }

            // Label / rename
            if (renamePrefabIdx == pi) {
                ImGui::SetNextItemWidth(iconSz);
                if (ImGui::InputText("##pren", renamePrefabBuf, 128,
                    ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll))
                { scene.Prefabs[pi].Name=renamePrefabBuf; renamePrefabIdx=-1; }
                if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) renamePrefabIdx=-1;
            } else {
                std::string lbl = scene.Prefabs[pi].Name;
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(lbl.c_str()); ImGui::PopTextWrapPos();
            }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // ---- MESH ASSETS (OBJ files) ----
    {
        while ((int)scene.MeshAssetFolders.size() < (int)scene.MeshAssets.size())
            scene.MeshAssetFolders.push_back(-1);
        int mcols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
        ImGui::Columns(mcols, "##meshcols", false);
        static int renameMeshIdx = -1;
        static char renameMeshBuf[128] = {};

        auto drawMeshIconDL = [&](ImVec2 p, bool sel) {
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 mn = {p.x+4, p.y+4}, mx = {p.x+iconSz-4, p.y+iconSz-4};
            const float r = 6.f;
            dl->AddRectFilledMultiColor(mn, mx,
                IM_COL32(60,60,70,255), IM_COL32(60,60,70,255),
                IM_COL32(40,40,50,255), IM_COL32(40,40,50,255));
            dl->AddRect(mn, mx, sel ? IM_COL32(200,200,255,255) : IM_COL32(100,100,120,120), r, 0, sel ? 2.f : 1.f);
            // "OBJ" label centred
            float fsz = iconSz * 0.25f;
            float px2 = p.x + iconSz * 0.5f - fsz * 0.6f;
            float py2 = p.y + iconSz * 0.5f - fsz * 0.5f;
            dl->AddText(nullptr, fsz, ImVec2(px2, py2), IM_COL32(200,200,220,220), "OBJ");
        };

        for (int oi = 0; oi < (int)scene.MeshAssets.size(); ++oi)
        {
            if (scene.MeshAssetFolders[oi] != m_CurrentFolder) continue;
            ImGui::PushID(60000 + oi);

            // Derive display name from path
            const std::string& oPath = scene.MeshAssets[oi];
            size_t sl = oPath.find_last_of("/\\");
            std::string oName = (sl != std::string::npos) ? oPath.substr(sl+1) : oPath;
            if (oName.size() > 4 && oName.substr(oName.size()-4) == ".obj")
                oName = oName.substr(0, oName.size()-4);

            bool sel = false; // no dedicated mesh selection for now
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
            bool clicked = ImGui::Button("##oicon", {iconSz, iconSz});
            ImGui::PopStyleColor(2);
            ImVec2 iconMin = ImGui::GetItemRectMin();
            drawMeshIconDL(iconMin, sel);

            // Context menu
            if (ImGui::BeginPopupContextItem("##octx")) {
                if (ImGui::MenuItem("Delete")) {
                    scene.MeshAssets.erase(scene.MeshAssets.begin() + oi);
                    scene.MeshAssetFolders.erase(scene.MeshAssetFolders.begin() + oi);
                    ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                    return ImGui::GetIO().WantCaptureMouse;
                }
                ImGui::EndPopup();
            }

            // Double-click = instantiate as entity
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                Mesh loaded = Mesh::LoadOBJ(oPath);
                Entity e = scene.CreateEntity(oName);
                int id = (int)e.GetID();
                scene.MeshRenderers[id].MeshPtr  = new Mesh(std::move(loaded));
                scene.MeshRenderers[id].MeshType = "obj:" + oPath;
                selectedEntity = id;
            }

            // Drag source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("MESH_ASSET", &oi, sizeof(int));
                ImGui::Text("OBJ: %s", oName.c_str());
                ImGui::EndDragDropSource();
            }

            { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(oName.c_str()); ImGui::PopTextWrapPos(); }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // ---- LUA SCRIPT ASSETS ----
    {
        while ((int)scene.ScriptAssetFolders.size() < (int)scene.ScriptAssets.size())
            scene.ScriptAssetFolders.push_back(-1);

        auto drawScriptIconDL = [&](ImVec2 p, bool sel) {
            auto* dl = ImGui::GetWindowDrawList();
            ImVec2 mn = {p.x+4, p.y+4}, mx = {p.x+iconSz-4, p.y+iconSz-4};
            const float r = 6.f;
            // Dark greenish background
            dl->AddRectFilledMultiColor(mn, mx,
                IM_COL32(30,55,35,255), IM_COL32(30,55,35,255),
                IM_COL32(20,40,25,255), IM_COL32(20,40,25,255));
            dl->AddRect(mn, mx, sel ? IM_COL32(100,255,140,255) : IM_COL32(70,180,90,120), r, 0, sel ? 2.f : 1.f);
            // "LUA" label centred
            float fsz = iconSz * 0.22f;
            float px2 = p.x + iconSz * 0.5f - fsz * 0.9f;
            float py2 = p.y + iconSz * 0.38f - fsz * 0.5f;
            dl->AddText(nullptr, fsz, ImVec2(px2, py2), IM_COL32(100,240,120,220), "LUA");
            // Small horizontal lines (code icon)
            float lx = p.x + iconSz*0.22f, lw = iconSz*0.56f;
            float ly = p.y + iconSz*0.60f;
            for (int li = 0; li < 3; li++) {
                dl->AddLine(ImVec2(lx, ly+li*4.5f), ImVec2(lx + lw*(li==2?0.65f:1.0f), ly+li*4.5f),
                            IM_COL32(80,180,100,140), 1.5f);
            }
        };

        int scols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
        ImGui::Columns(scols, "##scriptcols", false);

        for (int si = 0; si < (int)scene.ScriptAssets.size(); ++si)
        {
            if (scene.ScriptAssetFolders[si] != m_CurrentFolder) continue;
            ImGui::PushID(70000 + si);

            const std::string& sPath = scene.ScriptAssets[si];
            size_t sl2 = sPath.find_last_of("/\\");
            std::string sName = (sl2 != std::string::npos) ? sPath.substr(sl2+1) : sPath;
            // strip .lua extension for display
            if (sName.size() > 4 && sName.substr(sName.size()-4) == ".lua")
                sName = sName.substr(0, sName.size()-4);

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
            ImGui::Button("##sicon", {iconSz, iconSz});
            ImGui::PopStyleColor(2);
            ImVec2 iconMin = ImGui::GetItemRectMin();
            drawScriptIconDL(iconMin, false);

            // Context menu
            if (ImGui::BeginPopupContextItem("##sctx")) {
                if (ImGui::MenuItem("Delete")) {
                    scene.ScriptAssets.erase(scene.ScriptAssets.begin() + si);
                    scene.ScriptAssetFolders.erase(scene.ScriptAssetFolders.begin() + si);
                    ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End(); ImGui::PopStyleColor();
                    return ImGui::GetIO().WantCaptureMouse;
                }
                ImGui::EndPopup();
            }

            // Drag source â€” carries the script path
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("SCRIPT_ASSET", sPath.c_str(), sPath.size()+1);
                drawScriptIconDL({4,4}, false);
                ImGui::Text("LUA: %s", sName.c_str());
                ImGui::EndDragDropSource();
            }

            { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz); ImGui::TextUnformatted(sName.c_str()); ImGui::PopTextWrapPos(); }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // ---- SCENE FILES (.tscene) ----
    if (!m_SceneFiles.empty())
    {
        auto drawSceneIconDL = [&](ImVec2 p, bool isCurrent) {
            auto* dl  = ImGui::GetWindowDrawList();
            ImVec2 mn = {p.x + 4, p.y + 4};
            ImVec2 mx = {p.x + iconSz - 4, p.y + iconSz - 4};
            // Dark blue gradient
            dl->AddRectFilledMultiColor(mn, mx,
                IM_COL32(18, 45, 80, 255), IM_COL32(18, 45, 80, 255),
                IM_COL32(10, 25, 55, 255), IM_COL32(10, 25, 55, 255));
            // Subtle "sky" gradient in upper half
            dl->AddRectFilledMultiColor(mn, {mx.x, mn.y + (mx.y - mn.y) * 0.45f},
                IM_COL32(30, 80, 140, 180), IM_COL32(30, 80, 140, 180),
                IM_COL32(18, 45, 80, 0),   IM_COL32(18, 45, 80, 0));
            // "Ground" line
            float gy = mn.y + (mx.y - mn.y) * 0.60f;
            dl->AddLine({mn.x + 4, gy}, {mx.x - 4, gy}, IM_COL32(60, 160, 80, 180), 1.5f);
            // Small cube icon (3 strokes suggestion of a 3D box)
            float ex = p.x + iconSz * 0.38f, ey = p.y + iconSz * 0.30f, es = iconSz * 0.25f;
            dl->AddRect({ex, ey}, {ex + es, ey + es}, IM_COL32(140, 200, 255, 180), 2.f);
            dl->AddLine({ex, ey}, {ex - es * 0.3f, ey - es * 0.3f}, IM_COL32(140, 200, 255, 120));
            dl->AddLine({ex + es, ey}, {ex + es * 0.7f, ey - es * 0.3f}, IM_COL32(140, 200, 255, 120));
            dl->AddLine({ex + es, ey + es}, {ex + es * 1.7f, ey + es * 0.7f}, IM_COL32(140, 200, 255, 80));
            // Border
            dl->AddRect(mn, mx,
                isCurrent ? IM_COL32(80, 190, 255, 255) : IM_COL32(40, 100, 170, 120),
                6.f, 0, isCurrent ? 2.f : 1.f);
            // Active dot (top-right)
            if (isCurrent)
                dl->AddCircleFilled({mx.x - 6, mn.y + 6}, 4.f, IM_COL32(80, 230, 120, 230));
        };

        // New scene name modal (triggered from context menu below)
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5f, 0.5f});
        if (ImGui::BeginPopupModal("##newscenedlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Scene name:");
            ImGui::SetNextItemWidth(220.0f);
            if (m_ShowNewSceneDlg) { ImGui::SetKeyboardFocusHere(); m_ShowNewSceneDlg = false; }
            bool enter = ImGui::InputText("##nsname", m_NewSceneNameBuf, 128,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            bool ok = ImGui::Button("Create", {100, 0}) || enter;
            if (ok && m_NewSceneNameBuf[0] != '\0')
            { m_DlgNewSceneName = m_NewSceneNameBuf; m_PendingNewScene = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {80, 0})) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        int scnCols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cellW));
        ImGui::Columns(scnCols, "##scenecols", false);

        for (int si = 0; si < (int)m_SceneFiles.size(); ++si)
        {
            const std::string& spath = m_SceneFiles[si];
            // Filter by virtual folder
            {
                auto it = m_SceneFileFolders.find(spath);
                int sf = (it != m_SceneFileFolders.end()) ? it->second : -1;
                if (sf != m_CurrentFolder) continue;
            }
            size_t sls = spath.find_last_of("/\\");
            std::string sname = (sls != std::string::npos) ? spath.substr(sls + 1) : spath;
            if (sname.size() > 7 && sname.substr(sname.size() - 7) == ".tscene")
                sname = sname.substr(0, sname.size() - 7);
            bool isCurrent = (spath == m_CurrentScenePath);

            ImGui::PushID(80000 + si);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.4f, 0.7f, 0.3f));
            ImGui::Button("##scnasset", {iconSz, iconSz});
            if (isCurrent) ImGui::PopStyleColor();
            ImGui::PopStyleColor(2);
            ImVec2 iconMin = ImGui::GetItemRectMin();
            drawSceneIconDL(iconMin, isCurrent);

            // Drag source — drag the scene file path
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("SCENE_PATH", spath.c_str(), spath.size() + 1);
                drawSceneIconDL(ImGui::GetCursorScreenPos(), isCurrent);
                ImGui::Dummy({iconSz * 0.6f, iconSz * 0.6f});
                ImGui::Text("%s", sname.c_str());
                ImGui::EndDragDropSource();
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("##scnassetctx"))
            {
                if (ImGui::MenuItem("Open"))   { m_DlgOpenScenePath = spath; m_PendingOpenScene = true; }
                if (ImGui::MenuItem("Delete")) { m_DlgDeleteScenePath = spath; m_PendingDeleteScene = true; }
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            { m_DlgOpenScenePath = spath; m_PendingOpenScene = true; }

            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + iconSz);
            ImGui::TextUnformatted(sname.c_str());
            ImGui::PopTextWrapPos();
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // Full-area ENTITY drop target â€” covers the ENTIRE scroll child window
    {
        ImGuiWindow* childWin = ImGui::GetCurrentWindow();
        if (ImGui::BeginDragDropTargetCustom(childWin->ContentRegionRect, childWin->ID)) {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY")) {
                    int srcIdx = *(const int*)p->Data;
                    if (srcIdx >= 0 && srcIdx < (int)scene.EntityNames.size()) {
                        PrefabAsset prefab;
                        prefab.Name   = scene.EntityNames[srcIdx];
                        prefab.Folder = m_CurrentFolder;

                        // Recursive snapshot into Nodes (same logic as HierarchyPanel::CreatePrefab)
                        std::function<void(int,int)> snap = [&](int eIdx, int parentNodeIdx)
                        {
                            PrefabEntityData node;
                            node.ParentIdx = parentNodeIdx;
                            node.Name      = scene.EntityNames[eIdx];
                            node.Transform = scene.Transforms[eIdx];
                            if (parentNodeIdx == -1) node.Transform.Position = {0,0,0};
                            if (eIdx < (int)scene.MeshRenderers.size() && scene.MeshRenderers[eIdx].MeshPtr)
                                node.MeshType = scene.MeshRenderers[eIdx].MeshType;
                            if (eIdx < (int)scene.EntityMaterial.size() && scene.EntityMaterial[eIdx] >= 0) {
                                int mi2 = scene.EntityMaterial[eIdx];
                                if (mi2 < (int)scene.Materials.size()) node.MaterialName = scene.Materials[mi2].Name;
                            }
                            if (eIdx < (int)scene.Lights.size() && scene.Lights[eIdx].Active)
                                { node.HasLight = true; node.Light = scene.Lights[eIdx]; }
                            if (eIdx < (int)scene.GameCameras.size() && scene.GameCameras[eIdx].Active)
                                { node.HasCamera = true; node.Camera = scene.GameCameras[eIdx]; }
                            int myIdx = (int)prefab.Nodes.size();
                            prefab.Nodes.push_back(node);
                            if (eIdx < (int)scene.EntityChildren.size())
                                for (int child : scene.EntityChildren[eIdx]) snap(child, myIdx);
                        };
                        snap(srcIdx, -1);

                        std::string safeName = prefab.Name;
                        for (auto& c : safeName) if (c==' '||c=='/'||c=='\\') c='_';
                        std::filesystem::create_directories("assets/prefabs");
                        prefab.FilePath = "assets/prefabs/" + safeName + ".tprefab";
                        PrefabSerializer::Save(prefab, prefab.FilePath);
                        scene.Prefabs.push_back(prefab);
                        while ((int)scene.EntityIsPrefabInstance.size() <= srcIdx)
                            scene.EntityIsPrefabInstance.push_back(false);
                        scene.EntityIsPrefabInstance[srcIdx] = true;
                        while ((int)scene.EntityPrefabSource.size() <= srcIdx)
                            scene.EntityPrefabSource.push_back(-1);
                        // Source index will be the new last element in Prefabs
                        scene.EntityPrefabSource[srcIdx] = (int)scene.Prefabs.size() - 1;
                    }
                }
                ImGui::EndDragDropTarget();
        }
    }

    // Background right-click
    if (ImGui::BeginPopupContextWindow("##assetbg",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("New Folder")) {
            scene.Folders.push_back("New Folder");
            scene.FolderParents.push_back(m_CurrentFolder);
        }
        if (ImGui::MenuItem("New Material"))
        { tsu::MaterialAsset m; m.Name="Material"; m.Folder=m_CurrentFolder; scene.Materials.push_back(m); }
        if (ImGui::MenuItem("New Scene..."))
        {
            m_NewSceneNameBuf[0] = '\0';
            m_ShowNewSceneDlg    = true;
            ImGui::OpenPopup("##newscenedlg");
        }
        if (ImGui::MenuItem("New Script"))
        {
            // Generate a unique name
            int idx = (int)scene.ScriptAssets.size() + 1;
            std::filesystem::create_directories("assets/scripts");
            std::string name = "script_" + std::to_string(idx);
            std::string path = "assets/scripts/" + name + ".lua";
            // Avoid name collisions
            while (std::filesystem::exists(path)) {
                idx++;
                name  = "script_" + std::to_string(idx);
                path  = "assets/scripts/" + name + ".lua";
            }
            // Write a starter Lua file
            std::ofstream newf(path);
            if (newf.is_open()) {
                newf << "-- " << name << ".lua\n\n";
                newf << "-- Expose variables to the inspector with --@expose:\n";
                newf << "-- --@expose speed number 5.0\n\n";
                newf << "function OnStart()\nend\n\n";
                newf << "function OnUpdate(dt)\nend\n\n";
                newf << "function OnStop()\nend\n";
                newf.close();
            }
            scene.ScriptAssets.push_back(path);
            scene.ScriptAssetFolders.push_back(m_CurrentFolder);
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild(); // end ##assetscroll
    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
    return ImGui::GetIO().WantCaptureMouse;
}

void UIManager::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool UIManager::WantCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

// ---------------------------------------------------------------------------
// DrawSplash  â€”  full-screen animated splash for the first ~3.2 s
// ---------------------------------------------------------------------------
void UIManager::DrawSplash(float t, int w, int h)
{
    static constexpr float kDur         = 3.2f;
    static constexpr float kRingEnd     = 1.5f;
    static constexpr float kEyeStart    = 0.8f;
    static constexpr float kEyeEnd      = 1.4f;
    static constexpr float kSmileStart  = 1.4f;
    static constexpr float kSmileEnd    = 2.0f;
    static constexpr float kTextStart   = 2.0f;
    static constexpr float kTextEnd     = 2.8f;
    static constexpr float kFadeStart   = 2.9f;

    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; };
    float ringP  = clamp01(t / kRingEnd);
    float eyeA   = clamp01((t - kEyeStart)  / (kEyeEnd   - kEyeStart));
    float smileP = clamp01((t - kSmileStart) / (kSmileEnd - kSmileStart));
    float textA  = clamp01((t - kTextStart)  / (kTextEnd  - kTextStart));
    float fadeA  = 1.0f - clamp01((t - kFadeStart) / (kDur - kFadeStart));

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
    ImGui::SetNextWindowBgAlpha(fadeA);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, fadeA));
    ImGui::Begin("##splash", nullptr,
                 ImGuiWindowFlags_NoTitleBar   | ImGuiWindowFlags_NoResize    |
                 ImGuiWindowFlags_NoMove       | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();

    const float sz  = std::min((float)w, (float)h) * 0.38f;  // logo occupies 38% of the short edge
    const float cx  = w * 0.5f;
    const float cy  = h * 0.5f - sz * 0.15f;  // slightly above centre to leave room for text

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // background gradient-like dark rect (solid black at full alpha)
    dl->AddRectFilled(ImVec2(0,0), ImVec2((float)w,(float)h),
                      IM_COL32(0, 0, 0, (int)(fadeA * 255.0f)));

    // ---- animated logo ----
    DrawEngineLogo(dl, cx, cy, sz, IM_COL32(255,255,255,255),
                   ringP, eyeA, smileP);

    // ---- "TSU ENGINE" text ----
    if (textA > 0.0f)
    {
        const char* label = "TSU ENGINE";
        const float scale = sz / 100.0f * 0.6f;   // scale relative to logo
        const float fontSz = ImGui::GetFontSize();
        float textW = ImGui::CalcTextSize(label).x * scale;
        float textX = cx - textW * 0.5f;
        float textY = cy + sz * 0.56f;

        const ImU32 textCol = IM_COL32(255, 255, 255, (int)(textA * fadeA * 255.0f));
        // draw via DrawList so we can control alpha and scale manually
        dl->AddText(nullptr, fontSz * scale, ImVec2(textX, textY), textCol, label);
    }

    ImGui::End();
}

void UIManager::Log(const std::string& msg)
{
    m_ConsoleLogs.push_back(msg);
    if ((int)m_ConsoleLogs.size() > k_MaxConsoleLogs)
        m_ConsoleLogs.pop_front();
}

void UIManager::SetSceneFiles(const std::vector<std::string>& files, const std::string& currentPath)
{
    m_SceneFiles       = files;
    m_CurrentScenePath = currentPath;
}

void UIManager::OpenRoomEditor(int setIdx, int roomIdx)
{
    m_MazeRoomEditorOpen = true;
    m_EditingRoomSetIdx  = setIdx;
    m_EditingRoomIdx     = roomIdx;
    m_RequestViewportTab = 5;
    m_MazeSelectedBlock  = -1;
    m_MazeDoorMode       = false;
}

void UIManager::CloseRoomEditor()
{
    m_MazeRoomEditorOpen = false;
    m_EditingRoomSetIdx  = -1;
    m_EditingRoomIdx     = -1;
    if (m_ViewportTab == 5) m_ViewportTab = 0;
}

void UIManager::ConsumePendingLaunchMpHost(std::string& nick, int& port)
{
    nick = m_MpHostNickBuf;
    port = m_MpPort;
    m_PendingLaunchMpHost = false;
}

void UIManager::ConsumePendingLaunchMpClient(std::string& server, std::string& nick, int& port)
{
    server = m_MpServerBuf;
    nick = m_MpClientNickBuf;
    port = m_MpPort;
    m_PendingLaunchMpClient = false;
}

void UIManager::ConsumePendingLaunchMpPair(std::string& hostNick, std::string& clientNick, int& port)
{
    hostNick = m_MpHostNickBuf;
    clientNick = m_MpClientNickBuf;
    port = m_MpPort;
    m_PendingLaunchMpPair = false;
}

} // namespace tsu
