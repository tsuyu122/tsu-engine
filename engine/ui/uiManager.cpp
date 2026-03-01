#include "ui/uiManager.h"
#include "core/application.h"
#include "renderer/textureLoader.h"
#include "renderer/mesh.h"
#include "serialization/prefabSerializer.h"
#include "scene/entity.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <functional>

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

    // Side panels start just below the OpenGL toolbar (same Y as the tab strip),
    // filling the gap that previously appeared on the left/right of the camera tabs.
    const int sideY = topMenuH + topToolH;  // k_MenuBarH + k_ToolbarH = 58px
    const int sideH = winH - k_AssetH - sideY;

    // --- Panel resize handles (run every frame before panels are drawn) ---
    {
        const float handleW = 6.0f;
        const float hy0 = (float)sideY;
        const float hy1 = (float)(winH - k_AssetH);
        const float lx  = (float)m_PanelWidth;
        const float rx  = (float)(winW - m_PanelWidth);
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
    }
    else
    {
        m_Hierarchy.SetPrefabEditorState(false, -1, nullptr);
    }

    m_Hierarchy.Render(scene, selectedEntity,
                       0, sideY, m_PanelWidth, sideH);

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
    m_Inspector.SetPrefabEditorState(prefabEditorActive, m_EditingPrefabIdx, m_PrefabSelectedNode);
    m_Inspector.Render(scene, selectedEntity, selectedGroup,
                       winW - m_PanelWidth, sideY, m_PanelWidth, sideH,
                       inspMat, inspTex, inspPrf, multiSel);

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

        // Gizmo mode buttons (Move / Rotate / Scale) on the left
        {
            float btnSz = topH - 10.0f;
            auto gizmoBtn = [&](const char* label, GizmoMode mode) {
                bool active = (gizmoMode == mode);
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.25f, 0.55f, 0.85f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.65f, 0.95f, 1.0f));
                }
                if (ImGui::Button(label, ImVec2(btnSz, btnSz)))
                    gizmoMode = mode;
                if (active)
                    ImGui::PopStyleColor(2);
            };
            gizmoBtn("W", GizmoMode::Move);
            ImGui::SameLine(0, 2);
            gizmoBtn("E", GizmoMode::Scale);
            ImGui::SameLine(0, 2);
            gizmoBtn("R", GizmoMode::Rotate);
            ImGui::SameLine(0, 12);
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
            const float vx = (float)m_PanelWidth;
            const float vy = (float)topStackH;
            const float vw = (float)(winW - m_PanelWidth * 2);
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

    // ---- Breadcrumb navigation ----
    // Ensure FolderParents stays in sync with Folders
    while ((int)scene.FolderParents.size() < (int)scene.Folders.size())
        scene.FolderParents.push_back(-1);

    const bool inFolder = (m_CurrentFolder >= 0 && m_CurrentFolder < (int)scene.Folders.size());

    // Build chain: [grandparent, parent, current] (root → current)
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
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
            { int mi = *(const int*)pl->Data; if (mi < (int)scene.Materials.size()) scene.Materials[mi].Folder = -1; }
            if (auto* pl = ImGui::AcceptDragDropPayload("TEX_MOVE"))
            { int ti = *(const int*)pl->Data; if (ti < (int)scene.TextureFolders.size()) scene.TextureFolders[ti] = -1; }
            ImGui::EndDragDropTarget();
        }

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

    // Scrollable child for all asset icons — ensures drop zone covers full area
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
            if (ImGui::MenuItem("Rename")) { renameFolderIdx = fi; renameBuf[0]='\0'; strncpy(renameBuf,scene.Folders[fi].c_str(),127); }
            if (ImGui::MenuItem("Delete")) {
                // Move filhos para o pai desta pasta
                for (auto& m2 : scene.Materials)   if (m2.Folder==fi) m2.Folder=fp; else if(m2.Folder>fi) m2.Folder--;
                for (auto& tf : scene.TextureFolders) if(tf==fi) tf=fp; else if(tf>fi) tf--;
                for (auto& pfp : scene.FolderParents)  if(pfp==fi) pfp=fp; else if(pfp>fi) pfp--;
                scene.Folders.erase(scene.Folders.begin()+fi);
                scene.FolderParents.erase(scene.FolderParents.begin()+fi);
                if (m_SelectedFolder==fi) m_SelectedFolder=-1; else if(m_SelectedFolder>fi) m_SelectedFolder--;
                if (m_CurrentFolder==fi) m_CurrentFolder=fp;   else if(m_CurrentFolder>fi) m_CurrentFolder--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End();
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

    // ---- MATERIALS (filtered by current folder) ----
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

        // Context menu on material button
        if (ImGui::BeginPopupContextItem("##mctx")) {
            if (ImGui::MenuItem("Rename")) { renameMatIdx=mi; renameBuf[0]='\0'; strncpy(renameBuf,scene.Materials[mi].Name.c_str(),127); }
            if (inFolder && ImGui::MenuItem("Move to Root")) scene.Materials[mi].Folder=-1;
            if (!scene.Folders.empty() && ImGui::BeginMenu("Move to Folder")) {
                for (int fi2=0; fi2<(int)scene.Folders.size(); ++fi2)
                    if (fi2!=m_CurrentFolder && ImGui::MenuItem(scene.Folders[fi2].c_str())) scene.Materials[mi].Folder=fi2;
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete")) {
                scene.Materials.erase(scene.Materials.begin()+mi);
                if (m_SelectedMaterial==mi) m_SelectedMaterial=-1; else if(m_SelectedMaterial>mi) m_SelectedMaterial--;
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End();
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
                ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End();
                return ImGui::GetIO().WantCaptureMouse;
            }
            ImGui::EndPopup();
        }

        // Thumbnail: UV inverted ({0,1}→{1,0}) because texture was loaded with flip=true
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

        { std::string lbl=fname; if(lbl.size()>10) lbl=lbl.substr(0,9)+"\xe2\x80\xa6"; ImGui::TextUnformatted(lbl.c_str()); }
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
                    ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End();
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
                if (lbl.size() > 10) lbl = lbl.substr(0,9) + "\xe2\x80\xa6";
                ImGui::TextUnformatted(lbl.c_str());
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
                    ImGui::EndPopup(); ImGui::PopID(); ImGui::Columns(1); ImGui::EndChild(); ImGui::End();
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

            { std::string lbl = oName; if (lbl.size()>10) lbl=lbl.substr(0,9)+"\xe2\x80\xa6"; ImGui::TextUnformatted(lbl.c_str()); }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1);
    }

    // Full-area ENTITY drop target — covers the ENTIRE scroll child window
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
        ImGui::EndPopup();
    }

    ImGui::EndChild(); // end ##assetscroll
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
