#include "ui/hierarchyPanel.h"
#include "scene/entity.h"
#include "renderer/mesh.h"
#include <imgui.h>
#include <cstring>
#include <algorithm>

namespace tsu {

std::string HierarchyPanel::MakeUniqueName(const Scene& scene, const std::string& base)
{
    auto exists = [&](const std::string& n) -> bool {
        for (auto& name : scene.EntityNames)
            if (name == n) return true;
        for (auto& g : scene.Groups)
            if (!g.Name.empty() && g.Name == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 2; i < 999; i++)
    {
        std::string candidate = base + " (" + std::to_string(i) + ")";
        if (!exists(candidate)) return candidate;
    }
    return base;
}

void HierarchyPanel::CreateMeshEntity(Scene& scene, const std::string& type, int& selectedEntity)
{
    std::string name = MakeUniqueName(scene, type);
    Entity e    = scene.CreateEntity(name);
    int    id   = (int)e.GetID();

    std::string color = "#DDDDDD";
    Mesh* mesh = nullptr;
    if      (type == "Cube")     mesh = new Mesh(Mesh::CreateCube(color));
    else if (type == "Sphere")   mesh = new Mesh(Mesh::CreateSphere(color));
    else if (type == "Pyramid")  mesh = new Mesh(Mesh::CreatePyramid(color));
    else if (type == "Cylinder") mesh = new Mesh(Mesh::CreateCylinder(color));
    else if (type == "Capsule")  mesh = new Mesh(Mesh::CreateCapsule(color));
    scene.MeshRenderers[id].MeshPtr = mesh;

    if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
        scene.SetEntityGroup(id, m_SelectedGroup);

    selectedEntity   = id;
    m_RenamingEntity = id;
    strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
}

void HierarchyPanel::CreateCameraEntity(Scene& scene, int& selectedEntity)
{
    std::string name = MakeUniqueName(scene, "Camera");
    Entity e = scene.CreateEntity(name);
    int    id = (int)e.GetID();
    scene.GameCameras[id].Active = true;

    if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
        scene.SetEntityGroup(id, m_SelectedGroup);

    selectedEntity   = id;
    m_RenamingEntity = id;
    strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
}

// ----------------------------------------------------------------
// Insert-zone: thin drag-drop target rendered between sibling items.
// Accepts ENTITY payload and reorders within lst (RootOrder or
// group.ChildEntities or EntityChildren[parentEntity]).
// groupIdx / entityParentIdx control which parent the entity is
// moved into (-1 = ignored).
// ----------------------------------------------------------------
static void DrawInsertZone(Scene& scene, std::vector<int>& lst, int insertPos,
                            int groupIdx, int entityParentIdx)
{
    float w  = ImGui::GetContentRegionAvail().x;
    float py = ImGui::GetCursorScreenPos().y;
    ImGui::Dummy(ImVec2(w, 4.0f));
    if (!ImGui::BeginDragDropTarget()) return;

    // Visual insert line
    auto* dl = ImGui::GetWindowDrawList();
    float lx = ImGui::GetWindowPos().x + 4.0f;
    float rx = lx + w - 8.0f;
    dl->AddLine(ImVec2(lx, py + 1.5f), ImVec2(rx, py + 1.5f),
                IM_COL32(80, 160, 255, 230), 2.0f);

    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
    {
        int srcIdx = *(const int*)p->Data;

        // Remember original position in lst (before any structural change)
        auto preit  = std::find(lst.begin(), lst.end(), srcIdx);
        int  origPos = (preit != lst.end()) ? (int)(preit - lst.begin()) : -1;

        // Apply hierarchy change (this may append srcIdx to lst)
        if (entityParentIdx >= 0)
            scene.SetEntityParent(srcIdx, entityParentIdx);
        else if (groupIdx >= 0)
            scene.SetEntityGroup(srcIdx, groupIdx);
        else
            scene.UnparentEntity(srcIdx);

        // Reorder: find srcIdx (now at tail of lst), move to desired position
        auto it = std::find(lst.begin(), lst.end(), srcIdx);
        if (it != lst.end())
        {
            lst.erase(it);
            int adj = (origPos >= 0 && origPos < insertPos) ? insertPos - 1 : insertPos;
            adj = std::max(0, std::min(adj, (int)lst.size()));
            lst.insert(lst.begin() + adj, srcIdx);
        }
    }
    ImGui::EndDragDropTarget();
}

void HierarchyPanel::DrawEntityNode(Scene& scene, int entityIdx,
                                     int& selectedEntity, int& selectedGroup)
{
    const std::string& name = scene.EntityNames[entityIdx];

    // ---- Inline rename ----
    if (m_RenamingEntity == entityIdx)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.9f, 0.8f));
        bool confirmed = ImGui::InputText("##rename_e", m_RenameBuffer,
                                          sizeof(m_RenameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        if (confirmed || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() &&
                          ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()))
        {
            if (m_RenameBuffer[0] != '\0')
                scene.EntityNames[entityIdx] = m_RenameBuffer;
            m_RenamingEntity = -1;
        }
        return;
    }

    // ---- Entity children ----
    static const std::vector<int> kNoChildren;
    const std::vector<int>& entityChildren =
        (entityIdx < (int)scene.EntityChildren.size())
            ? scene.EntityChildren[entityIdx]
            : kNoChildren;
    bool isLeaf = entityChildren.empty();

    // ---- Tree node flags ----
    bool isSelected = (selectedEntity == entityIdx);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    else
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    bool isCamera = scene.GameCameras[entityIdx].Active;
    bool hasParent = (entityIdx < (int)scene.EntityParents.size())
                     && scene.EntityParents[entityIdx] >= 0;
    std::string prefix = isCamera ? "[Cam] " : (hasParent ? "  " : "  ");
    std::string label  = prefix + name + "##e" + std::to_string(entityIdx);

    bool open = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen())
    {
        selectedEntity = entityIdx;
        selectedGroup  = -1;
    }

    // ---- Drag source ----
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("ENTITY", &entityIdx, sizeof(int));
        ImGui::Text("Move: %s", name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---- Drop target: entity-on-entity = make child (no group) ----
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            int srcIdx = *(const int*)p->Data;
            if (srcIdx != entityIdx)
                scene.SetEntityParent(srcIdx, entityIdx);
        }
        ImGui::EndDragDropTarget();
    }

    // ---- Right-click context ----
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename"))
        {
            m_RenamingEntity = entityIdx;
            strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
        }
        if (hasParent && ImGui::MenuItem("Unparent"))
            scene.SetEntityParent(entityIdx, -1);
        if (ImGui::BeginMenu("Move to Group"))
        {
            if (ImGui::MenuItem("Root (no group/parent)")) scene.UnparentEntity(entityIdx);
            for (size_t g = 0; g < scene.Groups.size(); g++)
            {
                if (scene.Groups[g].Name.empty()) continue;
                if (ImGui::MenuItem(scene.Groups[g].Name.c_str()))
                    scene.SetEntityGroup(entityIdx, (int)g);
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    // ---- Recursive entity children (with insert zones) ----
    if (!isLeaf && open)
    {
        std::vector<int> childCopy = entityChildren;
        DrawInsertZone(scene, scene.EntityChildren[entityIdx], 0, -1, entityIdx);
        for (int ci = 0; ci < (int)childCopy.size(); ci++)
        {
            DrawEntityNode(scene, childCopy[ci], selectedEntity, selectedGroup);
            DrawInsertZone(scene, scene.EntityChildren[entityIdx], ci + 1, -1, entityIdx);
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::DrawGroupNode(Scene& scene, int groupIdx,
                                    int& selectedEntity, int& selectedGroup)
{
    SceneGroup& g = scene.Groups[groupIdx];
    if (g.Name.empty()) return;

    if (m_RenamingGroup == groupIdx)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.9f, 0.8f));
        bool confirmed = ImGui::InputText("##rename_g", m_RenameBuffer,
                                          sizeof(m_RenameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        if (confirmed || (!ImGui::IsItemFocused() && ImGui::IsMouseClicked(0)
                          && !ImGui::IsItemHovered()))
        {
            if (m_RenameBuffer[0] != '\0') g.Name = m_RenameBuffer;
            m_RenamingGroup = -1;
        }
        return;
    }

    bool isSelected = (selectedGroup == groupIdx && selectedEntity < 0);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (g.ChildEntities.empty() && g.ChildGroups.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;

    std::string label = "[Folder] " + g.Name + "##g" + std::to_string(groupIdx);
    bool open = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked(0))
    {
        selectedGroup  = groupIdx;
        selectedEntity = -1;
    }

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            int srcIdx = *(const int*)p->Data;
            scene.SetEntityGroup(srcIdx, groupIdx);
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename"))
        {
            m_RenamingGroup = groupIdx;
            strncpy(m_RenameBuffer, g.Name.c_str(), sizeof(m_RenameBuffer) - 1);
        }
        if (ImGui::MenuItem("Delete Group (unparents children)"))
        {
            if (selectedGroup == groupIdx) selectedGroup = -1;
            scene.DeleteGroup(groupIdx);
            if (open) ImGui::TreePop();
            return;
        }
        ImGui::EndPopup();
    }

    if (open)
    {
        std::vector<int> childGroupsCopy = g.ChildGroups;
        for (int cg : childGroupsCopy)
            DrawGroupNode(scene, cg, selectedEntity, selectedGroup);

        // Entity children with insert zones for reordering
        std::vector<int> childEntCopy = g.ChildEntities;
        DrawInsertZone(scene, g.ChildEntities, 0, groupIdx, -1);
        for (int ci = 0; ci < (int)childEntCopy.size(); ci++)
        {
            DrawEntityNode(scene, childEntCopy[ci], selectedEntity, selectedGroup);
            DrawInsertZone(scene, g.ChildEntities, ci + 1, groupIdx, -1);
        }
        ImGui::TreePop();
    }
}

void HierarchyPanel::Render(Scene& scene, int& selectedEntity,
                             int winX, int winY, int panelW, int panelH)
{
    int& selectedGroup = m_SelectedGroup;

    ImGui::SetNextWindowPos(ImVec2((float)winX, (float)winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)panelW, (float)panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove        |
                          ImGuiWindowFlags_NoResize       |
                          ImGuiWindowFlags_NoCollapse     |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Hierarchy", nullptr, wf);

    for (size_t g = 0; g < scene.Groups.size(); g++)
    {
        if (scene.Groups[g].Name.empty()) continue;
        if (scene.Groups[g].ParentGroup >= 0) continue;
        DrawGroupNode(scene, (int)g, selectedEntity, selectedGroup);
    }

    // Root entities in explicit display order, with insert zones for reordering
    {
        std::vector<int> rootCopy = scene.RootOrder;
        DrawInsertZone(scene, scene.RootOrder, 0, -1, -1);
        for (int ri = 0; ri < (int)rootCopy.size(); ri++)
        {
            int i = rootCopy[ri];
            DrawEntityNode(scene, i, selectedEntity, selectedGroup);
            DrawInsertZone(scene, scene.RootOrder, ri + 1, -1, -1);
        }
    }

    // Drop zone at the bottom = move to root
    ImGui::Dummy(ImVec2(-1.0f, 30.0f));
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            int srcIdx = *(const int*)p->Data;
            scene.UnparentEntity(srcIdx);
        }
        ImGui::EndDragDropTarget();
    }

    // Right-click on empty area = create context menu
    if (ImGui::BeginPopupContextWindow("##hier_ctx",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        // -- Objects --
        ImGui::TextDisabled("Objects");
        ImGui::Separator();
        if (ImGui::MenuItem("Cube"))     CreateMeshEntity(scene, "Cube",     selectedEntity);
        if (ImGui::MenuItem("Sphere"))   CreateMeshEntity(scene, "Sphere",   selectedEntity);
        if (ImGui::MenuItem("Pyramid"))  CreateMeshEntity(scene, "Pyramid",  selectedEntity);
        if (ImGui::MenuItem("Cylinder")) CreateMeshEntity(scene, "Cylinder", selectedEntity);
        if (ImGui::MenuItem("Capsule"))  CreateMeshEntity(scene, "Capsule",  selectedEntity);

        ImGui::Spacing();
        // -- Group --
        ImGui::TextDisabled("Group");
        ImGui::Separator();
        if (ImGui::MenuItem("Group"))
        {
            std::string gname = MakeUniqueName(scene, "Group");
            int parent = (selectedGroup >= 0) ? selectedGroup : -1;
            int gIdx   = scene.CreateGroup(gname, parent);
            selectedGroup  = gIdx;
            selectedEntity = -1;
            m_RenamingGroup = gIdx;
            strncpy(m_RenameBuffer, gname.c_str(), sizeof(m_RenameBuffer) - 1);
        }

        ImGui::Spacing();
        // -- Render --
        ImGui::TextDisabled("Render");
        ImGui::Separator();
        if (ImGui::MenuItem("Camera"))   CreateCameraEntity(scene, selectedEntity);

        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace tsu
