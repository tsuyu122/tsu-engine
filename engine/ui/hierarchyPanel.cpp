#include "ui/hierarchyPanel.h"
#include "scene/entity.h"
#include "renderer/mesh.h"
#include "serialization/prefabSerializer.h"
#include "input/inputmanager.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <cstdlib>

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
    std::string meshType;
    if      (type == "Cube")     { mesh = new Mesh(Mesh::CreateCube(color));     meshType = "cube"; }
    else if (type == "Sphere")   { mesh = new Mesh(Mesh::CreateSphere(color));   meshType = "sphere"; }
    else if (type == "Pyramid")  { mesh = new Mesh(Mesh::CreatePyramid(color));  meshType = "pyramid"; }
    else if (type == "Cylinder") { mesh = new Mesh(Mesh::CreateCylinder(color)); meshType = "cylinder"; }
    else if (type == "Capsule")  { mesh = new Mesh(Mesh::CreateCapsule(color));  meshType = "capsule"; }
    else if (type == "Plane")    { mesh = new Mesh(Mesh::CreatePlane(color));    meshType = "plane"; }
    scene.MeshRenderers[id].MeshPtr  = mesh;
    scene.MeshRenderers[id].MeshType = meshType;

    // If an entity is selected, spawn at its position and make it a child
    if (selectedEntity >= 0 && selectedEntity < (int)scene.Transforms.size())
    {
        scene.Transforms[id].Position = scene.Transforms[selectedEntity].Position;
        scene.SetEntityParent(id, selectedEntity);
    }
    else if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
    {
        scene.SetEntityGroup(id, m_SelectedGroup);
    }

    selectedEntity   = id;
    m_RenamingEntity = id;
    strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
}

void HierarchyPanel::CreateEmptyEntity(Scene& scene, int& selectedEntity)
{
    std::string name = MakeUniqueName(scene, "Empty");
    Entity e = scene.CreateEntity(name);
    int id = (int)e.GetID();
    if (selectedEntity >= 0 && selectedEntity < (int)scene.Transforms.size())
    {
        scene.Transforms[id].Position = scene.Transforms[selectedEntity].Position;
        scene.SetEntityParent(id, selectedEntity);
    }
    else if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
    {
        scene.SetEntityGroup(id, m_SelectedGroup);
    }
    selectedEntity = id;
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

    if (selectedEntity >= 0 && selectedEntity < (int)scene.Transforms.size())
    {
        scene.Transforms[id].Position = scene.Transforms[selectedEntity].Position;
        scene.SetEntityParent(id, selectedEntity);
    }
    else if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
    {
        scene.SetEntityGroup(id, m_SelectedGroup);
    }

    selectedEntity   = id;
    m_RenamingEntity = id;
    strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
}

void HierarchyPanel::CreateLightEntity(Scene& scene, LightType type, int& selectedEntity)
{
    static const char* typeNames[] = {
        "Directional Light", "Point Light", "Spot Light", "Area Light"
    };
    std::string name = MakeUniqueName(scene, typeNames[(int)type]);
    Entity e = scene.CreateEntity(name);
    int    id = (int)e.GetID();

    scene.Lights[id].Active  = true;
    scene.Lights[id].Enabled = true;
    scene.Lights[id].Type    = type;

    switch (type)
    {
    case LightType::Point:
        scene.Lights[id].Intensity = 1.5f;
        scene.Lights[id].Range     = 10.0f;
        break;
    case LightType::Spot:
        scene.Lights[id].Intensity   = 2.0f;
        scene.Lights[id].Range       = 15.0f;
        scene.Lights[id].InnerAngle  = 25.0f;
        scene.Lights[id].OuterAngle  = 40.0f;
        break;
    case LightType::Area:
        scene.Lights[id].Intensity = 2.0f;
        scene.Lights[id].Range     = 10.0f;
        scene.Lights[id].Width     = 2.0f;
        scene.Lights[id].Height    = 1.0f;
        break;
    default:  // Directional
        scene.Lights[id].Intensity = 1.0f;
        break;
    }

    if (selectedEntity >= 0 && selectedEntity < (int)scene.Transforms.size())
    {
        scene.Transforms[id].Position = scene.Transforms[selectedEntity].Position;
        scene.SetEntityParent(id, selectedEntity);
    }
    else if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
    {
        scene.SetEntityGroup(id, m_SelectedGroup);
    }

    selectedEntity   = id;
    m_RenamingEntity = id;
    strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
    m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
}

// ----------------------------------------------------------------
// CreatePrefab  — recursive snapshot of entity + all children
// ----------------------------------------------------------------
void HierarchyPanel::CreatePrefab(Scene& scene, int entityIdx)
{
    if (entityIdx < 0 || entityIdx >= (int)scene.EntityNames.size()) return;

    PrefabAsset prefab;
    prefab.Name = scene.EntityNames[entityIdx];

    // Recursive walk: snap entity tree into flat Nodes list, ParentIdx = index in list
    std::function<void(int, int)> snap = [&](int eIdx, int parentNodeIdx)
    {
        PrefabEntityData node;
        node.ParentIdx = parentNodeIdx;
        node.Name      = scene.EntityNames[eIdx];
        node.NodeGuid  = (int)prefab.Nodes.size();

        // Transform; root node stored with zero position (placed at spawn pos)
        node.Transform = scene.Transforms[eIdx];
        if (parentNodeIdx == -1)
            node.Transform.Position = {0.0f, 0.0f, 0.0f};

        // Mesh
        if (eIdx < (int)scene.MeshRenderers.size() && scene.MeshRenderers[eIdx].MeshPtr)
            node.MeshType = scene.MeshRenderers[eIdx].MeshType;

        // Material
        if (eIdx < (int)scene.EntityMaterial.size() && scene.EntityMaterial[eIdx] >= 0)
        {
            int mi = scene.EntityMaterial[eIdx];
            if (mi < (int)scene.Materials.size())
                node.MaterialName = scene.Materials[mi].Name;
        }

        // Light
        if (eIdx < (int)scene.Lights.size() && scene.Lights[eIdx].Active)
        {
            node.HasLight = true;
            node.Light    = scene.Lights[eIdx];
        }

        // Camera
        if (eIdx < (int)scene.GameCameras.size() && scene.GameCameras[eIdx].Active)
        {
            node.HasCamera = true;
            node.Camera    = scene.GameCameras[eIdx];
        }

        if (eIdx < (int)scene.Triggers.size() && scene.Triggers[eIdx].Active)
        {
            const auto& tr = scene.Triggers[eIdx];
            node.HasTrigger = true;
            node.TriggerSize = tr.Size;
            node.TriggerOffset = tr.Offset;
            node.TriggerChannel = tr.Channel;
            node.TriggerInsideValue = tr.InsideValue;
            node.TriggerOutsideValue = tr.OutsideValue;
            node.TriggerOneShot = tr.OneShot;
        }
        if (eIdx < (int)scene.Animators.size() && scene.Animators[eIdx].Active)
        {
            const auto& an = scene.Animators[eIdx];
            node.HasAnimator = true;
            node.AnimProp = (int)an.Property;
            node.AnimMode = (int)an.Mode;
            node.AnimEasing = (int)an.Easing;
            node.AnimFrom = an.From;
            node.AnimTo = an.To;
            node.AnimDuration = an.Duration;
            node.AnimDelay = an.Delay;
            node.AnimSpeed = an.PlaybackSpeed;
            node.AnimBlend = an.BlendWeight;
            node.AnimAutoPlay = an.AutoPlay;
        }
        if (eIdx < (int)scene.LuaScripts.size() && scene.LuaScripts[eIdx].Active)
        {
            node.HasLuaScript = true;
            node.LuaScriptPath = scene.LuaScripts[eIdx].ScriptPath;
            node.LuaExposedVars = scene.LuaScripts[eIdx].ExposedVars;
        }
        if (eIdx < (int)scene.AudioSources.size() && scene.AudioSources[eIdx].Active)
        {
            const auto& as = scene.AudioSources[eIdx];
            node.HasAudioSource = true;
            node.AudioClipPath = as.ClipPath;
            node.AudioLoop = as.Loop;
            node.AudioPlayOnStart = as.PlayOnStart;
            node.AudioSpatial = as.Spatial;
            node.AudioVolume = as.Volume;
            node.AudioPitch = as.Pitch;
            node.AudioMinDistance = as.MinDistance;
            node.AudioMaxDistance = as.MaxDistance;
            node.AudioOcclusionFactor = as.OcclusionFactor;
            node.AudioOutputChannel = as.OutputChannel;
        }
        if (eIdx < (int)scene.AudioBarriers.size() && scene.AudioBarriers[eIdx].Active)
        {
            const auto& ab = scene.AudioBarriers[eIdx];
            node.HasAudioBarrier = true;
            node.AudioBarrierSize = ab.Size;
            node.AudioBarrierOffset = ab.Offset;
            node.AudioBarrierOcclusion = ab.Occlusion;
        }

        int myIdx = (int)prefab.Nodes.size();
        prefab.Nodes.push_back(node);

        // Recurse into children
        if (eIdx < (int)scene.EntityChildren.size())
            for (int child : scene.EntityChildren[eIdx])
                snap(child, myIdx);
    };
    snap(entityIdx, -1);

    // Build safe filesystem name
    std::string safeName = prefab.Name;
    for (auto& c : safeName) if (c == ' ' || c == '/' || c == '\\') c = '_';

    std::filesystem::create_directories("assets/prefabs");
    prefab.FilePath = "assets/prefabs/" + safeName + ".tprefab";

    PrefabSerializer::Save(prefab, prefab.FilePath);
    scene.Prefabs.push_back(prefab);

    // Mark root entity as prefab instance (blue name)
    while ((int)scene.EntityIsPrefabInstance.size() <= entityIdx)
        scene.EntityIsPrefabInstance.push_back(false);
    scene.EntityIsPrefabInstance[entityIdx] = true;
    while ((int)scene.EntityPrefabSource.size() <= entityIdx)
        scene.EntityPrefabSource.push_back(-1);
    scene.EntityPrefabSource[entityIdx] = (int)scene.Prefabs.size() - 1;
    while ((int)scene.EntityPrefabNode.size() <= entityIdx)
        scene.EntityPrefabNode.push_back(0);
    scene.EntityPrefabNode[entityIdx] = 0;
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
    bool isInMulti  = std::find(m_MultiSelection.begin(), m_MultiSelection.end(), entityIdx) != m_MultiSelection.end();
    bool isSelected = (selectedEntity == entityIdx) || isInMulti;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    else
        flags |= ImGuiTreeNodeFlags_OpenOnArrow;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    bool isCamera = scene.GameCameras[entityIdx].Active;
    bool hasParent = (entityIdx < (int)scene.EntityParents.size())
                     && scene.EntityParents[entityIdx] >= 0;
    bool isPrefab = (entityIdx < (int)scene.EntityIsPrefabInstance.size())
                    && scene.EntityIsPrefabInstance[entityIdx];
    bool isOrphanedPrefab = false;
    if (isPrefab && entityIdx < (int)scene.EntityPrefabSource.size()) {
        int srcIdx = scene.EntityPrefabSource[entityIdx];
        if (srcIdx < 0 || srcIdx >= (int)scene.Prefabs.size())
            isOrphanedPrefab = true;
    }
    std::string prefix = isCamera ? "[Cam] " : (hasParent ? "  " : "  ");
    std::string label  = prefix + name + "##e" + std::to_string(entityIdx);

    // Tint prefab instance names: blue if valid, red if orphaned (source prefab deleted)
    if (isOrphanedPrefab)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
    else if (isPrefab)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.75f, 1.0f, 1.0f));
    bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (isPrefab || isOrphanedPrefab)
        ImGui::PopStyleColor();

    // Select only on clean click-release; suppress selection when the user is
    // dragging the item (e.g. onto an Inspector slot or rearranging hierarchy).
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && !ImGui::IsItemToggledOpen())
    {
        ImVec2 dd = ImGui::GetMouseDragDelta(0);
        if (fabsf(dd.x) + fabsf(dd.y) < 5.0f)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl)
            {
                // Ctrl+click: toggle in multi-selection
                auto it = std::find(m_MultiSelection.begin(), m_MultiSelection.end(), entityIdx);
                if (it != m_MultiSelection.end())
                    m_MultiSelection.erase(it);
                else
                {
                    // Ensure current primary is also in the set
                    if (selectedEntity >= 0 &&
                        std::find(m_MultiSelection.begin(), m_MultiSelection.end(), selectedEntity) == m_MultiSelection.end())
                        m_MultiSelection.push_back(selectedEntity);
                    m_MultiSelection.push_back(entityIdx);
                }
                m_LastSelected = entityIdx;
                selectedEntity = (!m_MultiSelection.empty()) ? m_MultiSelection.back() : entityIdx;
                selectedGroup  = -1;
            }
            else if (io.KeyShift && m_LastSelected >= 0)
            {
                // Shift+click: fill range by raw entity index
                m_MultiSelection.clear();
                int from = std::min(m_LastSelected, entityIdx);
                int to   = std::max(m_LastSelected, entityIdx);
                for (int ri = from; ri <= to; ri++)
                    if (ri < (int)scene.EntityNames.size() && !scene.EntityNames[ri].empty())
                        m_MultiSelection.push_back(ri);
                selectedEntity = entityIdx;
                selectedGroup  = -1;
            }
            else
            {
                // Plain click: single select, clear multi
                m_MultiSelection.clear();
                m_LastSelected = entityIdx;
                selectedEntity = entityIdx;
                selectedGroup  = -1;
            }
        }
    }

    // ---- Drag source ----
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("ENTITY", &entityIdx, sizeof(int));
        ImGui::Text("Move: %s", name.c_str());
        ImGui::EndDragDropSource();
    }

    // ---- Drop target: entity-on-entity = make child; material = assign material ----
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
        {
            int srcIdx = *(const int*)p->Data;
            if (srcIdx != entityIdx)
                scene.SetEntityParent(srcIdx, entityIdx);
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
        {
            int matIdx = *(const int*)p->Data;
            if (entityIdx < (int)scene.EntityMaterial.size())
                scene.EntityMaterial[entityIdx] = matIdx;
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
        if (isPrefab && ImGui::MenuItem("Unpack Prefab"))
        {
            if (entityIdx < (int)scene.EntityIsPrefabInstance.size())
                scene.EntityIsPrefabInstance[entityIdx] = false;
            if (entityIdx < (int)scene.EntityPrefabSource.size())
                scene.EntityPrefabSource[entityIdx] = -1;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate"))
        {
            // Deep-duplicate entity + children
            std::function<int(int, int)> dup = [&](int srcIdx, int parentIdx) -> int {
                std::string dupName = MakeUniqueName(scene, scene.EntityNames[srcIdx]);
                Entity ne = scene.CreateEntity(dupName);
                int nid = (int)ne.GetID();
                scene.Transforms[nid] = scene.Transforms[srcIdx];
                if (srcIdx < (int)scene.MeshRenderers.size() && scene.MeshRenderers[srcIdx].MeshPtr) {
                    const std::string& mt = scene.MeshRenderers[srcIdx].MeshType;
                    Mesh* mesh = nullptr;
                    std::string color = "#DDDDDD";
                    if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube(color));
                    else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere(color));
                    else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid(color));
                    else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder(color));
                    else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule(color));
                    else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane(color));
                    if (mesh) { scene.MeshRenderers[nid].MeshPtr = mesh; scene.MeshRenderers[nid].MeshType = mt; }
                }
                if (srcIdx < (int)scene.EntityMaterial.size())
                    scene.EntityMaterial[nid] = scene.EntityMaterial[srcIdx];
                if (srcIdx < (int)scene.Lights.size() && scene.Lights[srcIdx].Active)
                    scene.Lights[nid] = scene.Lights[srcIdx];
                if (srcIdx < (int)scene.GameCameras.size() && scene.GameCameras[srcIdx].Active)
                    scene.GameCameras[nid] = scene.GameCameras[srcIdx];
                if (parentIdx >= 0)
                    scene.SetEntityParent(nid, parentIdx);
                else if (srcIdx < (int)scene.EntityParents.size() && scene.EntityParents[srcIdx] >= 0)
                    scene.SetEntityParent(nid, scene.EntityParents[srcIdx]);
                // Recurse children
                if (srcIdx < (int)scene.EntityChildren.size())
                    for (int child : scene.EntityChildren[srcIdx])
                        dup(child, nid);
                return nid;
            };
            int dupId = dup(entityIdx, -1);
            selectedEntity = dupId;
        }
        if (ImGui::MenuItem("Create Prefab"))
            CreatePrefab(scene, entityIdx);
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Entity"))
        {
            if (selectedEntity == entityIdx) selectedEntity = -1;
            else if (selectedEntity > entityIdx) selectedEntity--;
            scene.DeleteEntity(entityIdx);
            ImGui::EndPopup();
            if (open && !isLeaf) ImGui::TreePop();
            return;
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

// ----------------------------------------------------------------
// Prefab editor: draw a prefab node as a tree node
// ----------------------------------------------------------------
void HierarchyPanel::DrawPrefabNodeTree(PrefabAsset& prefab, int nodeIdx, int depth)
{
    if (nodeIdx < 0 || nodeIdx >= (int)prefab.Nodes.size()) return;
    PrefabEntityData& node = prefab.Nodes[nodeIdx];

    // Collect children of this node
    std::vector<int> children;
    for (int i = 0; i < (int)prefab.Nodes.size(); ++i)
        if (prefab.Nodes[i].ParentIdx == nodeIdx) children.push_back(i);

    bool hasChildren = !children.empty();
    bool isSelected  = m_PrefabSelectedNode && (*m_PrefabSelectedNode == nodeIdx);

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    // Color: root = cyan, children = white
    if (depth == 0)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.85f, 1.0f, 1.0f));

    ImGui::PushID(nodeIdx);
    bool open = ImGui::TreeNodeEx(node.Name.c_str(), flags);
    if (depth == 0) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (m_PrefabSelectedNode) *m_PrefabSelectedNode = nodeIdx;
    }

    // ---- Inline rename for prefab nodes ----
    if (m_RenamingPrefabNode == nodeIdx)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.9f, 0.8f));
        bool confirmed = ImGui::InputText("##prefnode_rename", m_RenameBuffer,
                                          sizeof(m_RenameBuffer),
                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                          ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        if (confirmed || (!ImGui::IsItemActive() && !ImGui::IsItemFocused() &&
                          ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()))
        {
            if (m_RenameBuffer[0] != '\0') node.Name = m_RenameBuffer;
            m_RenamingPrefabNode = -1;
        }
    }

    // ---- Right-click context for each prefab node ----
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Rename"))
        {
            m_RenamingPrefabNode = nodeIdx;
            strncpy(m_RenameBuffer, node.Name.c_str(), sizeof(m_RenameBuffer) - 1);
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Child"))
        {
            auto addChildNode = [&](const char* name, const std::string& meshType) {
                PrefabEntityData child;
                child.Name      = name;
                child.ParentIdx = nodeIdx;
                child.MeshType  = meshType;
                child.Transform.Scale = glm::vec3(1.0f);
                int newIdx = (int)prefab.Nodes.size();
                prefab.Nodes.push_back(child);
                if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
            };
            if (ImGui::BeginMenu("3D Objects"))
            {
                if (ImGui::MenuItem("Cube"))     addChildNode("Cube",     "cube");
                if (ImGui::MenuItem("Sphere"))   addChildNode("Sphere",   "sphere");
                if (ImGui::MenuItem("Plane"))    addChildNode("Plane",    "plane");
                if (ImGui::MenuItem("Pyramid"))  addChildNode("Pyramid",  "pyramid");
                if (ImGui::MenuItem("Cylinder")) addChildNode("Cylinder", "cylinder");
                if (ImGui::MenuItem("Capsule"))  addChildNode("Capsule",  "capsule");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights"))
            {
                auto addChildLight = [&](const char* name, LightType lt) {
                    PrefabEntityData child;
                    child.Name      = name;
                    child.ParentIdx = nodeIdx;
                    child.HasLight  = true;
                    child.Light.Active  = true;
                    child.Light.Enabled = true;
                    child.Light.Type    = lt;
                    child.Light.Intensity = (lt == LightType::Directional) ? 1.0f : 1.5f;
                    child.Light.Range     = (lt == LightType::Spot) ? 15.0f : 10.0f;
                    if (lt == LightType::Spot) { child.Light.InnerAngle = 25.0f; child.Light.OuterAngle = 40.0f; }
                    if (lt == LightType::Area) { child.Light.Width = 2.0f; child.Light.Height = 1.0f; }
                    child.Transform.Scale = glm::vec3(1.0f);
                    int newIdx = (int)prefab.Nodes.size();
                    prefab.Nodes.push_back(child);
                    if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
                };
                if (ImGui::MenuItem("Directional Light")) addChildLight("Directional Light", LightType::Directional);
                if (ImGui::MenuItem("Point Light"))       addChildLight("Point Light",       LightType::Point);
                if (ImGui::MenuItem("Spot Light"))        addChildLight("Spot Light",        LightType::Spot);
                if (ImGui::MenuItem("Area Light"))        addChildLight("Area Light",        LightType::Area);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Hierarchy"))
            {
                if (ImGui::MenuItem("Group")) addChildNode("Group", "");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Render"))
            {
                if (ImGui::MenuItem("Camera"))
                {
                    PrefabEntityData child;
                    child.Name      = "Camera";
                    child.ParentIdx = nodeIdx;
                    child.HasCamera = true;
                    child.Camera.Active = true;
                    child.Transform.Scale = glm::vec3(1.0f);
                    int newIdx = (int)prefab.Nodes.size();
                    prefab.Nodes.push_back(child);
                    if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        // Unparent — move node to root level
        if (node.ParentIdx >= 0 && ImGui::MenuItem("Unparent"))
            node.ParentIdx = -1;

        // Duplicate — deep-copy node + children
        if (ImGui::MenuItem("Duplicate"))
        {
            std::function<int(int)> cloneNode = [&](int srcIdx) -> int {
                PrefabEntityData copy = prefab.Nodes[srcIdx];
                int newIdx = (int)prefab.Nodes.size();
                prefab.Nodes.push_back(copy);
                // Clone children (find children of srcIdx, clone them under newIdx)
                std::vector<int> childIdxs;
                for (int ci = 0; ci < (int)prefab.Nodes.size() - 1; ++ci)
                    if (prefab.Nodes[ci].ParentIdx == srcIdx) childIdxs.push_back(ci);
                for (int ci : childIdxs)
                {
                    int clonedChild = cloneNode(ci);
                    prefab.Nodes[clonedChild].ParentIdx = newIdx;
                }
                return newIdx;
            };
            int cloned = cloneNode(nodeIdx);
            if (m_PrefabSelectedNode) *m_PrefabSelectedNode = cloned;
        }

        ImGui::Separator();
        // Allow deleting any node (root included) when in room-interior override mode
        bool canDelete = (depth > 0 || m_PrefabOverride != nullptr);
        if (canDelete && ImGui::MenuItem("Delete Node"))
        {
            // Recursively remove this node and children
            std::function<void(int)> removeNode = [&](int ni) {
                // First remove children
                for (int i = (int)prefab.Nodes.size() - 1; i >= 0; --i)
                    if (prefab.Nodes[i].ParentIdx == ni) removeNode(i);
                // Fix parent indices
                for (auto& n : prefab.Nodes)
                    if (n.ParentIdx > ni) n.ParentIdx--;
                    else if (n.ParentIdx == ni) n.ParentIdx = -1;
                prefab.Nodes.erase(prefab.Nodes.begin() + ni);
                if (m_PrefabSelectedNode && *m_PrefabSelectedNode >= ni)
                    (*m_PrefabSelectedNode)--;
                if (m_PrefabSelectedNode && *m_PrefabSelectedNode < 0)
                    *m_PrefabSelectedNode = -1;
            };
            removeNode(nodeIdx);
            ImGui::EndPopup();
            ImGui::PopID();
            return;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();

    if (open && hasChildren) {
        for (int ci : children)
            DrawPrefabNodeTree(prefab, ci, depth + 1);
        ImGui::TreePop();
    }
}

void HierarchyPanel::Render(Scene& scene, int& selectedEntity,
                             int winX, int winY, int panelW, int panelH)
{
    int& selectedGroup = m_SelectedGroup;

    ImGui::SetNextWindowPos(ImVec2((float)winX, (float)winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)panelW, (float)panelH), ImGuiCond_Always);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove        |
                          ImGuiWindowFlags_NoResize       |
                          ImGuiWindowFlags_NoCollapse     |
                          ImGuiWindowFlags_NoBringToFrontOnFocus |
                          ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::Begin("Hierarchy", nullptr, wf);

    // Panel header
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.72f, 1.0f));
    ImGui::TextUnformatted("HIERARCHY");
    ImGui::PopStyleColor();
    ImGui::Separator();

    // Scrollable content area
    ImGui::BeginChild("##hierscroll", ImVec2(0, 0), false);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));

    // ---- Prefab / Room editor mode: show prefab nodes instead of scene hierarchy ----
    if (m_PrefabEditorActive &&
        (m_PrefabOverride || (m_PrefabEditorIdx >= 0 && m_PrefabEditorIdx < (int)scene.Prefabs.size())))
    {
        PrefabAsset& prefab = m_PrefabOverride ? *m_PrefabOverride
                                               : scene.Prefabs[m_PrefabEditorIdx];

        // Sync / Save buttons (only for real prefab editor, not room override)
        if (!m_PrefabOverride)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.82f, 1.0f));
            if (ImGui::Button("Sync Instances", ImVec2(-1, 0)) && m_SyncCallback)
                m_SyncCallback();
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.35f, 1.0f));
            if (ImGui::Button("Save to Disk", ImVec2(-1, 0)) && m_SaveCallback)
                m_SaveCallback();
            ImGui::PopStyleColor();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Prefab: %s", prefab.Name.c_str());
        }
        else
        {
            ImGui::TextColored(ImVec4(0.75f, 0.82f, 1.0f, 1.0f), "Room: %s", prefab.Name.c_str());
            ImGui::Separator();
            ImGui::TextDisabled("Blocks: %d  Doors: -- ", (int)prefab.Nodes.size());
        }
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                           m_PrefabOverride ? "Interior Nodes" : "Nodes");
        ImGui::Separator();

        // Draw prefab node tree starting from root nodes
        for (int ni = 0; ni < (int)prefab.Nodes.size(); ++ni)
            if (prefab.Nodes[ni].ParentIdx < 0)
                DrawPrefabNodeTree(prefab, ni, 0);

        // Click on empty background deselects node
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemHovered())
        {
            if (m_PrefabSelectedNode) *m_PrefabSelectedNode = -1;
        }

        // Right-click on empty area = create root-level nodes
        if (ImGui::BeginPopupContextWindow("##prefab_ctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            auto addRootNode = [&](const char* name, const std::string& meshType) {
                PrefabEntityData child;
                child.Name      = name;
                child.ParentIdx = -1;
                child.MeshType  = meshType;
                child.Transform.Scale = glm::vec3(1.0f);
                int newIdx = (int)prefab.Nodes.size();
                prefab.Nodes.push_back(child);
                if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
            };
            if (ImGui::BeginMenu("3D Objects"))
            {
                if (ImGui::MenuItem("Cube"))     addRootNode("Cube",     "cube");
                if (ImGui::MenuItem("Sphere"))   addRootNode("Sphere",   "sphere");
                if (ImGui::MenuItem("Plane"))    addRootNode("Plane",    "plane");
                if (ImGui::MenuItem("Pyramid"))  addRootNode("Pyramid",  "pyramid");
                if (ImGui::MenuItem("Cylinder")) addRootNode("Cylinder", "cylinder");
                if (ImGui::MenuItem("Capsule"))  addRootNode("Capsule",  "capsule");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Lights"))
            {
                auto addRootLight = [&](const char* name, LightType lt) {
                    PrefabEntityData child;
                    child.Name      = name;
                    child.ParentIdx = -1;
                    child.HasLight  = true;
                    child.Light.Active  = true;
                    child.Light.Enabled = true;
                    child.Light.Type    = lt;
                    child.Light.Intensity = (lt == LightType::Directional) ? 1.0f : 1.5f;
                    child.Light.Range     = (lt == LightType::Spot) ? 15.0f : 10.0f;
                    if (lt == LightType::Spot) { child.Light.InnerAngle = 25.0f; child.Light.OuterAngle = 40.0f; }
                    if (lt == LightType::Area) { child.Light.Width = 2.0f; child.Light.Height = 1.0f; }
                    child.Transform.Scale = glm::vec3(1.0f);
                    int newIdx = (int)prefab.Nodes.size();
                    prefab.Nodes.push_back(child);
                    if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
                };
                if (ImGui::MenuItem("Directional Light")) addRootLight("Directional Light", LightType::Directional);
                if (ImGui::MenuItem("Point Light"))       addRootLight("Point Light",       LightType::Point);
                if (ImGui::MenuItem("Spot Light"))        addRootLight("Spot Light",        LightType::Spot);
                if (ImGui::MenuItem("Area Light"))        addRootLight("Area Light",        LightType::Area);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Hierarchy"))
            {
                if (ImGui::MenuItem("Group")) addRootNode("Group", "");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Render"))
            {
                if (ImGui::MenuItem("Camera"))
                {
                    PrefabEntityData child;
                    child.Name      = "Camera";
                    child.ParentIdx = -1;
                    child.HasCamera = true;
                    child.Camera.Active = true;
                    child.Transform.Scale = glm::vec3(1.0f);
                    int newIdx = (int)prefab.Nodes.size();
                    prefab.Nodes.push_back(child);
                    if (m_PrefabSelectedNode) *m_PrefabSelectedNode = newIdx;
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        // ---- Keyboard shortcuts for prefab/room editor (Ctrl+C, Ctrl+V, Delete) ----
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && m_PrefabSelectedNode)
        {
            bool ctrl = InputManager::IsKeyPressed(Key::LeftControl) || InputManager::IsKeyPressed(Key::RightControl);

            // Ctrl+C — copy selected prefab node
            if (ctrl && InputManager::IsKeyDown(Key::C) && *m_PrefabSelectedNode >= 0)
            {
                m_ClipboardPrefabNodes.clear();
                m_ClipboardPrefabNodes.push_back(*m_PrefabSelectedNode);
            }

            // Ctrl+V — paste (duplicate) copied prefab node
            if (ctrl && InputManager::IsKeyDown(Key::V) && !m_ClipboardPrefabNodes.empty())
            {
                for (int srcIdx : m_ClipboardPrefabNodes)
                {
                    if (srcIdx >= 0 && srcIdx < (int)prefab.Nodes.size())
                    {
                        std::function<int(int)> cloneNode = [&](int si) -> int {
                            PrefabEntityData copy = prefab.Nodes[si];
                            int newIdx = (int)prefab.Nodes.size();
                            prefab.Nodes.push_back(copy);
                            std::vector<int> childIdxs;
                            for (int ci = 0; ci < (int)prefab.Nodes.size() - 1; ++ci)
                                if (prefab.Nodes[ci].ParentIdx == si) childIdxs.push_back(ci);
                            for (int ci : childIdxs)
                            {
                                int clonedChild = cloneNode(ci);
                                prefab.Nodes[clonedChild].ParentIdx = newIdx;
                            }
                            return newIdx;
                        };
                        int cloned = cloneNode(srcIdx);
                        *m_PrefabSelectedNode = cloned;
                    }
                }
            }

            // Delete — delete selected prefab node
            if (InputManager::IsKeyDown(Key::Delete) && *m_PrefabSelectedNode >= 0)
            {
                int nodeIdx = *m_PrefabSelectedNode;
                bool canDelete = (prefab.Nodes[nodeIdx].ParentIdx >= 0 || m_PrefabOverride != nullptr);
                if (canDelete)
                {
                    std::function<void(int)> removeNode = [&](int ni) {
                        for (int i = (int)prefab.Nodes.size() - 1; i >= 0; --i)
                            if (prefab.Nodes[i].ParentIdx == ni) removeNode(i);
                        for (auto& n : prefab.Nodes)
                            if (n.ParentIdx > ni) n.ParentIdx--;
                            else if (n.ParentIdx == ni) n.ParentIdx = -1;
                        prefab.Nodes.erase(prefab.Nodes.begin() + ni);
                        if (*m_PrefabSelectedNode >= ni) (*m_PrefabSelectedNode)--;
                        if (*m_PrefabSelectedNode < 0) *m_PrefabSelectedNode = -1;
                    };
                    removeNode(nodeIdx);
                }
            }
        }

        ImGui::PopStyleVar();
        ImGui::EndChild(); // ##hierscroll
        ImGui::End();
        ImGui::PopStyleColor(); // WindowBg
        return;
    }

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

    // Drop zone at the bottom = move to root (fill remaining space)
    // Uses Dummy (not InvisibleButton) so it doesn't eat right-click events
    {
        float remH = ImGui::GetContentRegionAvail().y;
        if (remH > 4.0f)  // only draw if there's actual remaining space
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float dummyW = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(dummyW, remH));
            ImVec2 p1 = ImVec2(p0.x + dummyW, p0.y + remH);
            ImGuiWindow* win = ImGui::GetCurrentWindow();
            if (ImGui::BeginDragDropTargetCustom(ImRect(p0, p1), win->ID))
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
                {
                    int srcIdx = *(const int*)p->Data;
                    scene.UnparentEntity(srcIdx);
                }
                ImGui::EndDragDropTarget();
            }
        }
    }

    // Left-click on empty hierarchy background deselects entity/group
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered())
    {
        selectedEntity   = -1;
        selectedGroup    = -1;
        m_MultiSelection.clear();
        m_LastSelected   = -1;
    }

    // Right-click on empty area = create context menu
    if (ImGui::BeginPopupContextWindow("##hier_ctx",
        ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
    {
        if (ImGui::BeginMenu("3D Objects"))
        {
            if (ImGui::MenuItem("Empty Object")) CreateEmptyEntity(scene, selectedEntity);
            if (ImGui::MenuItem("Cube"))     CreateMeshEntity(scene, "Cube",     selectedEntity);
            if (ImGui::MenuItem("Sphere"))   CreateMeshEntity(scene, "Sphere",   selectedEntity);
            if (ImGui::MenuItem("Plane"))        CreateMeshEntity(scene, "Plane",    selectedEntity);
            if (ImGui::MenuItem("Pyramid"))  CreateMeshEntity(scene, "Pyramid",  selectedEntity);
            if (ImGui::MenuItem("Cylinder")) CreateMeshEntity(scene, "Cylinder", selectedEntity);
            if (ImGui::MenuItem("Capsule"))  CreateMeshEntity(scene, "Capsule",  selectedEntity);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Lights"))
        {
            if (ImGui::MenuItem("Directional Light")) CreateLightEntity(scene, LightType::Directional, selectedEntity);
            if (ImGui::MenuItem("Point Light"))       CreateLightEntity(scene, LightType::Point,       selectedEntity);
            if (ImGui::MenuItem("Spot Light"))        CreateLightEntity(scene, LightType::Spot,        selectedEntity);
            if (ImGui::MenuItem("Area Light"))        CreateLightEntity(scene, LightType::Area,        selectedEntity);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Hierarchy"))
        {
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render"))
        {
            if (ImGui::MenuItem("Camera"))   CreateCameraEntity(scene, selectedEntity);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Multiplayer Manager"))
        {
            std::string name = MakeUniqueName(scene, "Multiplayer Manager");
            Entity e = scene.CreateEntity(name);
            int id = (int)e.GetID();
            if (selectedEntity >= 0 && selectedEntity < (int)scene.Transforms.size())
            {
                scene.Transforms[id].Position = scene.Transforms[selectedEntity].Position;
                scene.SetEntityParent(id, selectedEntity);
            }
            else if (m_SelectedGroup >= 0 && m_SelectedGroup < (int)scene.Groups.size())
                scene.SetEntityGroup(id, m_SelectedGroup);
            selectedEntity   = id;
            m_RenamingEntity = id;
            strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
            m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
            if (id >= 0 && id < (int)scene.MultiplayerManagers.size())
            {
                scene.MultiplayerManagers[id].Active = true;
                scene.MultiplayerManagers[id].Enabled = true;
            }
        }

        ImGui::EndPopup();
    }

    // ---- Keyboard shortcuts (Ctrl+C, Ctrl+V, Delete) ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        bool ctrl = InputManager::IsKeyPressed(Key::LeftControl) || InputManager::IsKeyPressed(Key::RightControl);

        // Ctrl+C — copy selected entities
        if (ctrl && InputManager::IsKeyDown(Key::C))
        {
            m_ClipboardEntities.clear();
            if (!m_MultiSelection.empty())
                m_ClipboardEntities = m_MultiSelection;
            else if (selectedEntity >= 0)
                m_ClipboardEntities.push_back(selectedEntity);
        }

        // Ctrl+V — paste (duplicate) copied entities
        if (ctrl && InputManager::IsKeyDown(Key::V) && !m_ClipboardEntities.empty())
        {
            std::function<int(int, int, Scene&)> dup = [&](int srcIdx, int parentIdx, Scene& sc) -> int {
                std::string dupName = MakeUniqueName(sc, sc.EntityNames[srcIdx]);
                Entity ne = sc.CreateEntity(dupName);
                int nid = (int)ne.GetID();
                sc.Transforms[nid] = sc.Transforms[srcIdx];
                if (srcIdx < (int)sc.MeshRenderers.size() && sc.MeshRenderers[srcIdx].MeshPtr) {
                    const std::string& mt = sc.MeshRenderers[srcIdx].MeshType;
                    Mesh* mesh = nullptr;
                    std::string color = "#DDDDDD";
                    if      (mt == "cube")     mesh = new Mesh(Mesh::CreateCube(color));
                    else if (mt == "sphere")   mesh = new Mesh(Mesh::CreateSphere(color));
                    else if (mt == "pyramid")  mesh = new Mesh(Mesh::CreatePyramid(color));
                    else if (mt == "cylinder") mesh = new Mesh(Mesh::CreateCylinder(color));
                    else if (mt == "capsule")  mesh = new Mesh(Mesh::CreateCapsule(color));
                    else if (mt == "plane")    mesh = new Mesh(Mesh::CreatePlane(color));
                    if (mesh) { sc.MeshRenderers[nid].MeshPtr = mesh; sc.MeshRenderers[nid].MeshType = mt; }
                }
                if (srcIdx < (int)sc.EntityMaterial.size())
                    sc.EntityMaterial[nid] = sc.EntityMaterial[srcIdx];
                if (srcIdx < (int)sc.Lights.size() && sc.Lights[srcIdx].Active)
                    sc.Lights[nid] = sc.Lights[srcIdx];
                if (srcIdx < (int)sc.GameCameras.size() && sc.GameCameras[srcIdx].Active)
                    sc.GameCameras[nid] = sc.GameCameras[srcIdx];
                if (parentIdx >= 0)
                    sc.SetEntityParent(nid, parentIdx);
                else if (srcIdx < (int)sc.EntityParents.size() && sc.EntityParents[srcIdx] >= 0)
                    sc.SetEntityParent(nid, sc.EntityParents[srcIdx]);
                if (srcIdx < (int)sc.EntityChildren.size())
                    for (int child : sc.EntityChildren[srcIdx])
                        dup(child, nid, sc);
                return nid;
            };
            int lastDup = -1;
            for (int src : m_ClipboardEntities)
            {
                if (src >= 0 && src < (int)scene.Transforms.size())
                    lastDup = dup(src, -1, scene);
            }
            if (lastDup >= 0) selectedEntity = lastDup;
        }

        // Delete — delete selected entities (single or multi-selection)
        if (InputManager::IsKeyDown(Key::Delete))
        {
            std::vector<int> toDelete;
            if (!m_MultiSelection.empty())
                toDelete = m_MultiSelection;
            else if (selectedEntity >= 0)
                toDelete.push_back(selectedEntity);

            if (!toDelete.empty())
            {
                // Sort descending so deletions don't shift remaining indices
                std::sort(toDelete.begin(), toDelete.end(), std::greater<int>());
                // Remove duplicates
                toDelete.erase(std::unique(toDelete.begin(), toDelete.end()), toDelete.end());
                for (int eid : toDelete)
                {
                    if (eid >= 0 && eid < (int)scene.Transforms.size())
                        scene.DeleteEntity(eid);
                }
                selectedEntity = -1;
                m_MultiSelection.clear();
                m_LastSelected = -1;
            }
        }
    }

    ImGui::PopStyleVar(); // ItemSpacing
    ImGui::EndChild(); // ##hierscroll
    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

} // namespace tsu
