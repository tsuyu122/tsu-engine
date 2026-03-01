#include "ui/uiManager.h"
#include "renderer/textureLoader.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace tsu {

void UIManager::Init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.ItemSpacing    = ImVec2(6, 4);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.15f, 0.95f);
    style.Colors[ImGuiCol_Header]          = ImVec4(0.25f, 0.40f, 0.70f, 0.6f);
    style.Colors[ImGuiCol_HeaderHovered]   = ImVec4(0.35f, 0.55f, 0.85f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive]    = ImVec4(0.45f, 0.65f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_FrameBg]         = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_Button]          = ImVec4(0.25f, 0.40f, 0.70f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered]   = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    style.Colors[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.15f, 0.25f, 0.45f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
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
// DrawEngineLogo  —  smiley-face logo drawn via ImDrawList
//   cx/cy    : screen-space centre
//   sz       : bounding square size (maps SVG viewBox 200x200 → sz×sz)
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

    // --- Smile arc (centre ≈ 100,100 in SVG, r=60, 10°→170°) ---
    // In ImGui Y-down coords: 0°=right, 90°=down  → arc 10°..170° is the bottom smile curve.
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
                       std::vector<std::string>& droppedFiles)
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
            const float sz   = barH - 4.0f;  // 2px padding top+bottom
            ImDrawList* dl   = ImGui::GetWindowDrawList();
            ImVec2      pos  = ImGui::GetCursorScreenPos();
            float cx = pos.x + sz * 0.5f;
            float cy = pos.y + sz * 0.5f;
            DrawEngineLogo(dl, cx, cy, sz, IM_COL32(255,255,255,255));
            ImGui::Dummy(ImVec2(sz + 4.0f, sz));
        }
        if (ImGui::BeginMenu("File"))
        {
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
        ImGui::EndMainMenuBar();
    }

    // --- Processar arquivos arrastados (esta função conhece m_CurrentFolder) ---
    {
        static const char* imgExts[] = {".png",".jpg",".jpeg",".bmp",".tga",".hdr",nullptr};
        for (auto& p : droppedFiles)
        {
            size_t dp = p.find_last_of('.');
            if (dp == std::string::npos) continue;
            std::string ext = p.substr(dp);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            bool isImg = false;
            for (int k = 0; imgExts[k]; ++k) if (ext == imgExts[k]) { isImg = true; break; }
            if (!isImg) continue;
            if (std::find(scene.Textures.begin(), scene.Textures.end(), p) != scene.Textures.end()) continue;
            scene.Textures.push_back(p);
            scene.TextureIDs.push_back(tsu::LoadTexture(p));
            scene.TextureFolders.push_back(m_CurrentFolder);
        }
        droppedFiles.clear();
    }

    // Side panels start just below the OpenGL toolbar (same Y as the tab strip),
    // filling the gap that previously appeared on the left/right of the camera tabs.
    const int sideY = topMenuH + topToolH;  // k_MenuBarH + k_ToolbarH = 58px
    const int sideH = winH - k_AssetH - sideY;

    // --- Panel resize handles (run every frame before panels are drawn) ---
    {
        const float handleW = 4.0f;
        const float hy0 = (float)sideY;
        const float hy1 = (float)(winH - k_AssetH);
        const float lx  = (float)m_PanelWidth;
        const float rx  = (float)(winW - m_PanelWidth);
        ImGuiIO& rio = ImGui::GetIO();
        float mx2 = rio.MousePos.x;
        float my2 = rio.MousePos.y;
        bool nearLeft  = (mx2>=lx-handleW && mx2<=lx+handleW && my2>hy0 && my2<hy1);
        bool nearRight = (mx2>=rx-handleW && mx2<=rx+handleW && my2>hy0 && my2<hy1);
        if (rio.MouseDown[0] && !rio.WantCaptureMouseUnlessPopupClose)
        {
            if (nearLeft  && !m_DraggingRight) m_DraggingLeft  = true;
            if (nearRight && !m_DraggingLeft)  m_DraggingRight = true;
        }
        if (!rio.MouseDown[0]) { m_DraggingLeft = false; m_DraggingRight = false; }
        if (m_DraggingLeft)
        {
            int nw = (int)mx2;
            m_PanelWidth = nw < 150 ? 150 : (nw > winW/3 ? winW/3 : nw);
        }
        if (m_DraggingRight)
        {
            int nw = winW - (int)mx2;
            m_PanelWidth = nw < 150 ? 150 : (nw > winW/3 ? winW/3 : nw);
        }
        if (nearLeft || nearRight || m_DraggingLeft || m_DraggingRight)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    // Hierarchy
    m_Hierarchy.Render(scene, selectedEntity,
                       0, sideY, m_PanelWidth, sideH);

    int selectedGroup = m_Hierarchy.GetSelectedGroup();

    // Inspector
    int inspMat = (selectedEntity < 0 && selectedGroup < 0) ? m_SelectedMaterial : -1;
    m_Inspector.Render(scene, selectedEntity, selectedGroup,
                       winW - m_PanelWidth, sideY, m_PanelWidth, sideH,
                       inspMat);

    // ---------------------------------------------------------------
    // Top panel: camera tabs (same style as Assets/Console)
    // ---------------------------------------------------------------
    {
        const float topH = (float)topTabsH;
        const float y = (float)(topMenuH + topToolH);
        ImGui::SetNextWindowPos (ImVec2((float)m_PanelWidth, y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)(winW - m_PanelWidth * 2), topH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.90f);
        ImGui::Begin("##cameratop", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

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
            m_RequestViewportTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    // Project settings view (third viewport mode)
    if (m_ViewportTab == 2)
    {
        const float vx = (float)m_PanelWidth;
        const float vy = (float)topStackH;
        const float vw = (float)(winW - m_PanelWidth * 2);
        const float vh = (float)(winH - k_AssetH - topStackH);

        ImGui::SetNextWindowPos(ImVec2(vx, vy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(vw, vh), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);
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
    // Bottom panel: Assets / Console tabs
    // ---------------------------------------------------------------
    ImGui::SetNextWindowPos (ImVec2(0.0f, (float)(winH - k_AssetH)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)k_AssetH),   ImGuiCond_Always);
    ImGui::Begin("##bottompanel", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

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
        ImGui::PopStyleColor();
        ImGui::End();
        return ImGui::GetIO().WantCaptureMouse;
    }

    // ---- Assets (m_BottomTab == 0) ----

    // ---- Breadcrumb multi-nível ----
    // Garante que FolderParents esteja em sincronia com Folders
    while ((int)scene.FolderParents.size() < (int)scene.Folders.size())
        scene.FolderParents.push_back(-1);

    const bool inFolder = (m_CurrentFolder >= 0 && m_CurrentFolder < (int)scene.Folders.size());

    // Constrói cadeia: [avô, pai, atual] (raiz → atual)
    {
        std::vector<int> chain;
        int cur = m_CurrentFolder;
        while (cur >= 0 && cur < (int)scene.Folders.size()) {
            chain.push_back(cur);
            cur = scene.FolderParents[cur];
        }
        std::reverse(chain.begin(), chain.end());

        // Segmento "Assets" → raiz
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.12f));
        if (ImGui::SmallButton("Assets")) m_CurrentFolder = -1;
        ImGui::PopStyleColor(2);
        // Drop em "Assets" = mover para raiz
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
            { int mi = *(const int*)pl->Data; if (mi < (int)scene.Materials.size()) scene.Materials[mi].Folder = -1; }
            if (auto* pl = ImGui::AcceptDragDropPayload("TEX_MOVE"))
            { int ti = *(const int*)pl->Data; if (ti < (int)scene.TextureFolders.size()) scene.TextureFolders[ti] = -1; }
            ImGui::EndDragDropTarget();
        }

        // Segmentos intermédios/final
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
                // Drop neste segmento = mover item para esta pasta
                if (ImGui::BeginDragDropTarget()) {
                    if (auto* pl = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
                    { int mi=(*(const int*)pl->Data); if(mi<(int)scene.Materials.size()) scene.Materials[mi].Folder=seg; }
                    if (auto* pl = ImGui::AcceptDragDropPayload("TEX_MOVE"))
                    { int ti=(*(const int*)pl->Data); if(ti<(int)scene.TextureFolders.size()) scene.TextureFolders[ti]=seg; }
                    ImGui::EndDragDropTarget();
                }
            }
        }
    }
    ImGui::Separator();

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
    auto drawMatIconDL = [&](ImVec2 p, glm::vec3 c) {
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({p.x+8,p.y+8},{p.x+iconSz-8,p.y+iconSz-8},
            IM_COL32((int)(c.r*255),(int)(c.g*255),(int)(c.b*255),255), 6.f);
        dl->AddRect({p.x+8,p.y+8},{p.x+iconSz-8,p.y+iconSz-8}, IM_COL32(255,255,255,100), 6.f);
    };

    // ---- PASTAS (filhos diretos de m_CurrentFolder) ----
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

        // Context menu no BOTÃO (logo após o item)
        if (ImGui::BeginPopupContextItem("##fctx")) {
            if (ImGui::MenuItem("Renomear")) { renameFolderIdx = fi; renameBuf[0]='\0'; strncpy(renameBuf,scene.Folders[fi].c_str(),127); }
            if (ImGui::MenuItem("Apagar")) {
                // Move filhos para o pai desta pasta
                for (auto& m2 : scene.Materials)   if (m2.Folder==fi) m2.Folder=fp; else if(m2.Folder>fi) m2.Folder--;
                for (auto& tf : scene.TextureFolders) if(tf==fi) tf=fp; else if(tf>fi) tf--;
                for (auto& pfp : scene.FolderParents)  if(pfp==fi) pfp=fp; else if(pfp>fi) pfp--;
                scene.Folders.erase(scene.Folders.begin()+fi);
                scene.FolderParents.erase(scene.FolderParents.begin()+fi);
                if (m_SelectedFolder==fi) m_SelectedFolder=-1; else if(m_SelectedFolder>fi) m_SelectedFolder--;
                if (m_CurrentFolder==fi) m_CurrentFolder=fp;   else if(m_CurrentFolder>fi) m_CurrentFolder--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::End();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        drawFolderIconDL(iconMin);
        if (clicked) { m_SelectedFolder=fi; m_SelectedMaterial=-1; selectedEntity=-1; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) m_CurrentFolder=fi;

        // Drag source
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("FOLDER_IDX", &fi, sizeof(int));
            drawFolderIconDL({4,4}); ImGui::Text("%s", scene.Folders[fi].c_str());
            ImGui::EndDragDropSource();
        }
        // Drop target: mover textura/material para esta pasta
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
                std::string tp((const char*)pl->Data, pl->DataSize-1);
                auto it = std::find(scene.Textures.begin(), scene.Textures.end(), tp);
                if (it != scene.Textures.end()) {
                    int ti2=(int)(it-scene.Textures.begin());
                    while((int)scene.TextureFolders.size()<=ti2) scene.TextureFolders.push_back(-1);
                    scene.TextureFolders[ti2]=fi;
                }
            }
            if (auto* pl = ImGui::AcceptDragDropPayload("TEX_MOVE"))
            { int ti2=*(const int*)pl->Data; if(ti2<(int)scene.TextureFolders.size()) scene.TextureFolders[ti2]=fi; }
            if (auto* pl = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
            { int mi2=*(const int*)pl->Data; if(mi2<(int)scene.Materials.size()) scene.Materials[mi2].Folder=fi; }
            ImGui::EndDragDropTarget();
        }

        // Label / rename
        if (renameFolderIdx==fi) {
            if (!renameBuf[0]) strncpy(renameBuf, scene.Folders[fi].c_str(), 127);
            ImGui::SetNextItemWidth(iconSz);
            if (ImGui::InputText("##fren", renameBuf, 128,
                ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll))
            { scene.Folders[fi]=renameBuf; renameBuf[0]='\0'; renameFolderIdx=-1; }
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) { renameBuf[0]='\0'; renameFolderIdx=-1; }
        } else {
            std::string lbl=scene.Folders[fi];
            if (lbl.size()>10) lbl=lbl.substr(0,9)+"\xe2\x80\xa6";
            ImGui::TextUnformatted(lbl.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            { renameFolderIdx=fi; renameBuf[0]='\0'; }
        }
        ImGui::NextColumn(); ImGui::PopID();
    }

    // ---- MATERIAIS (filtrado pela pasta atual) ----
    for (int mi = 0; mi < (int)scene.Materials.size(); ++mi)
    {
        if (scene.Materials[mi].Folder != m_CurrentFolder) continue;
        ImGui::PushID(20000 + mi);
        bool sel = (m_SelectedMaterial==mi);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));
        if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f,0.7f,0.3f,0.3f));
        bool clicked = ImGui::Button("##micon", {iconSz, iconSz});
        if (sel) ImGui::PopStyleColor();
        ImGui::PopStyleColor(2);
        ImVec2 iconMin = ImGui::GetItemRectMin();

        // Context menu no BOTÃO
        if (ImGui::BeginPopupContextItem("##mctx")) {
            if (ImGui::MenuItem("Renomear")) { renameMatIdx=mi; renameBuf[0]='\0'; strncpy(renameBuf,scene.Materials[mi].Name.c_str(),127); }
            if (inFolder && ImGui::MenuItem("Mover para raiz")) scene.Materials[mi].Folder=-1;
            if (!scene.Folders.empty() && ImGui::BeginMenu("Mover para pasta")) {
                for (int fi2=0; fi2<(int)scene.Folders.size(); ++fi2)
                    if (fi2!=m_CurrentFolder && ImGui::MenuItem(scene.Folders[fi2].c_str())) scene.Materials[mi].Folder=fi2;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Apagar")) {
                scene.Materials.erase(scene.Materials.begin()+mi);
                if (m_SelectedMaterial==mi) m_SelectedMaterial=-1; else if(m_SelectedMaterial>mi) m_SelectedMaterial--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::End();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        drawMatIconDL(iconMin, scene.Materials[mi].Color);
        if (clicked) { m_SelectedMaterial=mi; m_SelectedFolder=-1; selectedEntity=-1; }

        // Drag source MATERIAL_IDX - único payload para pastas, entidades e inspector
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
            if (!renameBuf[0]) strncpy(renameBuf, scene.Materials[mi].Name.c_str(), 127);
            ImGui::SetNextItemWidth(iconSz);
            if (ImGui::InputText("##mren", renameBuf, 128,
                ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll))
            { scene.Materials[mi].Name=renameBuf; renameBuf[0]='\0'; renameMatIdx=-1; }
            if (!ImGui::IsItemActive() && !ImGui::IsItemFocused()) { renameBuf[0]='\0'; renameMatIdx=-1; }
        } else {
            std::string lbl=scene.Materials[mi].Name;
            if (lbl.size()>10) lbl=lbl.substr(0,9)+"\xe2\x80\xa6";
            ImGui::TextUnformatted(lbl.c_str());
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            { renameMatIdx=mi; renameBuf[0]='\0'; }
        }
        ImGui::NextColumn(); ImGui::PopID();
    }

    // ---- TEXTURAS (filtrado pela pasta atual) ----
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
        ImGui::Button("##ticon", {iconSz, iconSz});
        ImGui::PopStyleColor(2);
        ImVec2 iconMin = ImGui::GetItemRectMin();

        // Context menu no BOTÃO
        if (ImGui::BeginPopupContextItem("##tctx")) {
            if (inFolder && ImGui::MenuItem("Mover para raiz")) scene.TextureFolders[ti]=-1;
            if (!scene.Folders.empty() && ImGui::BeginMenu("Mover para pasta")) {
                for (int fi2=0; fi2<(int)scene.Folders.size(); ++fi2)
                    if (fi2!=m_CurrentFolder && ImGui::MenuItem(scene.Folders[fi2].c_str())) scene.TextureFolders[ti]=fi2;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Apagar")) {
                if (scene.TextureIDs[ti]>0) glDeleteTextures(1, &scene.TextureIDs[ti]);
                scene.Textures.erase(scene.Textures.begin()+ti);
                scene.TextureIDs.erase(scene.TextureIDs.begin()+ti);
                scene.TextureFolders.erase(scene.TextureFolders.begin()+ti);
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::End();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        // Thumbnail: UV invertido ({0,1}→{1,0}) porque textura foi carregada com flip=true
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

        // Drag source: TEXTURE_PATH para inspector/pastas
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("TEXTURE_PATH", fullPath.c_str(), fullPath.size()+1);
            if (scene.TextureIDs[ti]>0)
                ImGui::Image((ImTextureID)(intptr_t)scene.TextureIDs[ti], {40,40}, {0,1},{1,0});
            else ImGui::Text("Textura");
            ImGui::Text("%s", fname.c_str());
            ImGui::EndDragDropSource();
        }

        { std::string lbl=fname; if(lbl.size()>10) lbl=lbl.substr(0,9)+"\xe2\x80\xa6"; ImGui::TextUnformatted(lbl.c_str()); }
        ImGui::NextColumn(); ImGui::PopID();
    }

    ImGui::Columns(1);

    // Background right-click
    if (ImGui::BeginPopupContextWindow("##assetbg",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::MenuItem("Nova Pasta")) {
            scene.Folders.push_back("Nova Pasta");
            scene.FolderParents.push_back(m_CurrentFolder); // subfolder se dentro de uma pasta
        }
        if (ImGui::MenuItem("Novo Material"))
        { tsu::MaterialAsset m; m.Name="Material"; m.Folder=m_CurrentFolder; scene.Materials.push_back(m); }
        ImGui::EndPopup();
    }

    ImGui::End();
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
// DrawSplash  —  full-screen animated splash for the first ~3.2 s
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

} // namespace tsu
