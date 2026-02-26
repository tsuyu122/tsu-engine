#include "ui/uiManager.h"
#include "renderer/textureLoader.h"
#include <glad/glad.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <algorithm>

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

bool UIManager::Render(Scene& scene, int& selectedEntity, int winW, int winH,
                       std::vector<std::string>& droppedFiles)
{
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

    // Hierarchy
    m_Hierarchy.Render(scene, selectedEntity,
                       0, 0, k_PanelWidth, winH - k_AssetH);

    int selectedGroup = m_Hierarchy.GetSelectedGroup();

    // Inspector
    int inspMat = (selectedEntity < 0 && selectedGroup < 0) ? m_SelectedMaterial : -1;
    m_Inspector.Render(scene, selectedEntity, selectedGroup,
                       winW - k_PanelWidth, 0, k_PanelWidth, winH - k_AssetH,
                       inspMat);

    // ---------------------------------------------------------------
    // Asset Browser
    // ---------------------------------------------------------------
    ImGui::SetNextWindowPos (ImVec2(0.0f, (float)(winH - k_AssetH)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)winW, (float)k_AssetH),   ImGuiCond_Always);
    ImGui::Begin("Assets", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoTitleBar);

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

} // namespace tsu
