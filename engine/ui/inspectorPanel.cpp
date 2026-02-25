#include "ui/inspectorPanel.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>

namespace tsu {

static const char* ColliderTypeNames[] = {
    "None", "Box", "Sphere", "Capsule", "Pyramid"
};

// ----------------------------------------------------------------
// Helper: labeled row in a 2-column table
// ----------------------------------------------------------------

static bool TableDragFloat3(const char* label, float* v, float speed,
                             float vmin = 0.0f, float vmax = 0.0f)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
    char id[64]; snprintf(id, sizeof(id), "##%s", label);
    return ImGui::DragFloat3(id, v, speed, vmin, vmax);
}

static bool TableDragFloat1(const char* label, float* v, float speed,
                             float vmin = 0.0f, float vmax = 0.0f)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
    char id[64]; snprintf(id, sizeof(id), "##%s", label);
    return ImGui::DragFloat(id, v, speed, vmin, vmax);
}

// ----------------------------------------------------------------
// Helper: component section header with right-click Remove and
//         up/down reorder arrows.
// Returns true if the section is open. Sets removedOut = true when
// the user selects "Remove Component" from the context menu.
// ----------------------------------------------------------------

static bool DrawCompHeader(const char* label, const char* uid,
                            int orderIdx, std::vector<int>& order,
                            bool& removedOut)
{
    removedOut = false;

    // Breathing room between sections
    ImGui::Spacing();
    ImGui::Spacing();

    bool open = ImGui::CollapsingHeader(label,
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    // Right-click on header → Remove
    char ctxId[64]; snprintf(ctxId, sizeof(ctxId), "##rmctx_%s", uid);
    if (ImGui::BeginPopupContextItem(ctxId))
    {
        if (ImGui::MenuItem("Remove Component")) removedOut = true;
        ImGui::EndPopup();
    }

    // Drag the header itself to reorder (hold and drag to another header)
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
    {
        ImGui::SetDragDropPayload("COMP_REORDER", &orderIdx, sizeof(int));
        ImGui::Text("Move: %s", label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("COMP_REORDER"))
        {
            int fromIdx = *(const int*)p->Data;
            if (fromIdx != orderIdx &&
                fromIdx >= 0 && fromIdx < (int)order.size() &&
                orderIdx >= 0 && orderIdx < (int)order.size())
                std::swap(order[fromIdx], order[orderIdx]);
        }
        ImGui::EndDragDropTarget();
    }

    return open;
}

// ----------------------------------------------------------------
// GetOrBuildOrder – sync stored order with active components
// ----------------------------------------------------------------

std::vector<int>& InspectorPanel::GetOrBuildOrder(Scene& scene, int idx)
{
    std::vector<int>& stored = m_CompOrder[idx];

    auto isActive = [&](int comp) -> bool {
        switch (comp) {
            case COMP_CAMERA:   return scene.GameCameras[idx].Active;
            case COMP_GRAVITY:  return scene.RigidBodies[idx].HasGravityModule;
            case COMP_COLLIDER: return scene.RigidBodies[idx].HasColliderModule;
            default: return false;
        }
    };

    // Remove components that are no longer active
    stored.erase(std::remove_if(stored.begin(), stored.end(),
        [&](int c) { return !isActive(c); }), stored.end());

    // Append any newly-active components not yet in order
    auto addMissing = [&](int comp) {
        if (isActive(comp) &&
            std::find(stored.begin(), stored.end(), comp) == stored.end())
            stored.push_back(comp);
    };
    addMissing(COMP_CAMERA);
    addMissing(COMP_GRAVITY);
    addMissing(COMP_COLLIDER);

    return stored;
}

// ----------------------------------------------------------------
// Transform section (always shown, no remove)
// ----------------------------------------------------------------

void InspectorPanel::DrawTransformSection(TransformComponent& t, const char* label)
{
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (!ImGui::BeginTable("##tf", 2)) return;
    ImGui::TableSetupColumn("lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("val", ImGuiTableColumnFlags_WidthStretch);

    float pos[3] = { t.Position.x, t.Position.y, t.Position.z };
    if (TableDragFloat3("Position", pos, 0.05f))
        t.Position = { pos[0], pos[1], pos[2] };

    float rot[3] = { t.Rotation.x, t.Rotation.y, t.Rotation.z };
    if (TableDragFloat3("Rotation", rot, 0.5f))
        t.Rotation = { rot[0], rot[1], rot[2] };

    float scl[3] = { t.Scale.x, t.Scale.y, t.Scale.z };
    if (TableDragFloat3("Scale", scl, 0.01f, 0.001f, 100.0f))
        t.Scale = { scl[0], scl[1], scl[2] };

    ImGui::EndTable();
}

// ----------------------------------------------------------------
// Game Camera section
// ----------------------------------------------------------------

bool InspectorPanel::DrawCameraSection(GameCameraComponent& gc,
                                        int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Game Camera", "cam", orderIdx, order, removed);
    if (removed) { gc.Active = false; return false; }
    if (!open)   return true;

    if (!ImGui::BeginTable("##cam", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("FOV",   &gc.FOV,   0.5f, 10.0f, 170.0f);
    TableDragFloat1("Near",  &gc.Near,  0.01f, 0.001f, 10.0f);
    TableDragFloat1("Far",   &gc.Far,   1.0f, 1.0f, 10000.0f);
    TableDragFloat1("Yaw",   &gc.Yaw,   0.5f);
    TableDragFloat1("Pitch", &gc.Pitch, 0.5f, -89.0f, 89.0f);
    ImGui::EndTable();
    return true;
}

// ----------------------------------------------------------------
// Gravity module
// ----------------------------------------------------------------

bool InspectorPanel::DrawGravitySection(RigidBodyComponent& rb,
                                         int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Gravity", "grav", orderIdx, order, removed);
    if (removed) { rb.HasGravityModule = false; rb.Velocity = {0,0,0}; return false; }
    if (!open)   return true;

    ImGui::Checkbox("Use Gravity",  &rb.UseGravity);
    ImGui::Checkbox("Is Kinematic", &rb.IsKinematic);

    if (!ImGui::BeginTable("##grav", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("Mass", &rb.Mass, 0.05f, 0.001f, 1000.0f);
    TableDragFloat1("Drag", &rb.Drag, 0.001f, 0.0f, 1.0f);
    ImGui::EndTable();

    ImGui::TextDisabled("Velocity: %.2f  %.2f  %.2f",
        rb.Velocity.x, rb.Velocity.y, rb.Velocity.z);
    ImGui::TextDisabled("Grounded: %s", rb.IsGrounded ? "yes" : "no");
    return true;
}

// ----------------------------------------------------------------
// Collider module
// ----------------------------------------------------------------

bool InspectorPanel::DrawColliderSection(Scene& scene, int entityIdx,
                                          int orderIdx, std::vector<int>& order)
{
    RigidBodyComponent& rb = scene.RigidBodies[entityIdx];

    bool removed = false;
    bool open    = DrawCompHeader("Collider", "col", orderIdx, order, removed);
    if (removed) { rb.HasColliderModule = false; return false; }
    if (!open)   return true;

    int colliderIdx = (int)rb.Collider;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##coltype", &colliderIdx, ColliderTypeNames, 5))
        rb.Collider = (ColliderType)colliderIdx;

    if (!ImGui::BeginTable("##col", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

    if (rb.Collider == ColliderType::Box || rb.Collider == ColliderType::Pyramid)
    {
        float sz[3] = { rb.ColliderSize.x, rb.ColliderSize.y, rb.ColliderSize.z };
        if (TableDragFloat3("Size", sz, 0.01f, 0.001f, 100.0f))
            rb.ColliderSize = { sz[0], sz[1], sz[2] };
    }
    else if (rb.Collider == ColliderType::Sphere)
    {
        TableDragFloat1("Radius", &rb.ColliderRadius, 0.01f, 0.001f, 100.0f);
    }
    else if (rb.Collider == ColliderType::Capsule)
    {
        TableDragFloat1("Radius", &rb.ColliderRadius, 0.01f, 0.001f, 100.0f);
        TableDragFloat1("Height", &rb.ColliderHeight, 0.01f, 0.001f, 100.0f);
    }

    float off[3] = { rb.ColliderOffset.x, rb.ColliderOffset.y, rb.ColliderOffset.z };
    if (TableDragFloat3("Offset", off, 0.01f))
        rb.ColliderOffset = { off[0], off[1], off[2] };

    ImGui::EndTable();
    return true;
}

// ----------------------------------------------------------------
// Add Component popup
// ----------------------------------------------------------------

void InspectorPanel::DrawAddComponentMenu(Scene& scene, int entityIdx,
                                           std::vector<int>& order)
{
    RigidBodyComponent&  rb = scene.RigidBodies[entityIdx];
    GameCameraComponent& gc = scene.GameCameras[entityIdx];

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
        ImGui::OpenPopup("##addcomp");

    if (ImGui::BeginPopup("##addcomp"))
    {
        bool any = false;
        if (!gc.Active)
        {
            if (ImGui::MenuItem("Game Camera")) { gc.Active = true; order.push_back(COMP_CAMERA); }
            any = true;
        }
        if (!rb.HasGravityModule)
        {
            if (ImGui::MenuItem("Gravity"))  { rb.HasGravityModule  = true; order.push_back(COMP_GRAVITY); }
            any = true;
        }
        if (!rb.HasColliderModule)
        {
            if (ImGui::MenuItem("Collider")) { rb.HasColliderModule = true; order.push_back(COMP_COLLIDER); }
            any = true;
        }
        if (!any) ImGui::TextDisabled("No more components available.");
        ImGui::EndPopup();
    }
}

// ----------------------------------------------------------------
// Group order management
// ----------------------------------------------------------------

std::vector<int>& InspectorPanel::GetOrBuildGroupOrder(SceneGroup& group, int groupIdx)
{
    std::vector<int>& stored = m_GroupCompOrder[groupIdx];

    auto isActive = [&](int comp) -> bool {
        switch (comp) {
            case COMP_GRAVITY:  return group.RigidBody.HasGravityModule;
            case COMP_COLLIDER: return group.RigidBody.HasColliderModule;
            case COMP_CAMERA:   return group.GameCamera.Active;
            default: return false;
        }
    };

    stored.erase(std::remove_if(stored.begin(), stored.end(),
        [&](int c) { return !isActive(c); }), stored.end());

    auto addMissing = [&](int comp) {
        if (isActive(comp) &&
            std::find(stored.begin(), stored.end(), comp) == stored.end())
            stored.push_back(comp);
    };
    addMissing(COMP_CAMERA);
    addMissing(COMP_GRAVITY);
    addMissing(COMP_COLLIDER);
    return stored;
}

// ----------------------------------------------------------------
// Add-component popup for groups
// ----------------------------------------------------------------

void InspectorPanel::DrawAddGroupComponentMenu(SceneGroup& group, std::vector<int>& order)
{
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("+ Add Component", ImVec2(-1, 0)))
        ImGui::OpenPopup("##addcomp_grp");

    if (ImGui::BeginPopup("##addcomp_grp"))
    {
        bool any = false;
        if (!group.GameCamera.Active)
        {
            if (ImGui::MenuItem("Game Camera")) { group.GameCamera.Active = true; order.push_back(COMP_CAMERA); }
            any = true;
        }
        if (!group.RigidBody.HasGravityModule)
        {
            if (ImGui::MenuItem("Gravity"))  { group.RigidBody.HasGravityModule  = true; order.push_back(COMP_GRAVITY); }
            any = true;
        }
        if (!group.RigidBody.HasColliderModule)
        {
            if (ImGui::MenuItem("Collider")) { group.RigidBody.HasColliderModule = true; order.push_back(COMP_COLLIDER); }
            any = true;
        }
        if (!any) ImGui::TextDisabled("No more components available.");
        ImGui::EndPopup();
    }
}

// ----------------------------------------------------------------
// Group Transform + components section
// ----------------------------------------------------------------

void InspectorPanel::DrawGroupSection(SceneGroup& group, int groupIdx)
{
    ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Group: %s", group.Name.c_str());
    ImGui::Separator();
    DrawTransformSection(group.Transform, "Group Transform (local)");
    ImGui::TextDisabled("Children entities: %d", (int)group.ChildEntities.size());
    ImGui::TextDisabled("Children groups:   %d", (int)group.ChildGroups.size());

    // Group components
    std::vector<int>& order = GetOrBuildGroupOrder(group, groupIdx);
    for (int i = 0; i < (int)order.size(); )
    {
        bool kept = true;
        switch (order[i])
        {
            case COMP_CAMERA:
                kept = DrawCameraSection(group.GameCamera, i, order);
                break;
            case COMP_GRAVITY:
                kept = DrawGravitySection(group.RigidBody, i, order);
                break;
            case COMP_COLLIDER:
            {
                // Collider for groups – inline draw (no scene entity index)
                bool removed2 = false;
                bool open2    = DrawCompHeader("Collider", "grpcol", i, order, removed2);
                if (removed2) { group.RigidBody.HasColliderModule = false; kept = false; break; }
                if (open2)
                {
                    int ci = (int)group.RigidBody.Collider;
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##grpcoltype", &ci, ColliderTypeNames, 5))
                        group.RigidBody.Collider = (ColliderType)ci;
                }
                break;
            }
        }
        if (!kept) order.erase(order.begin() + i);
        else       ++i;
    }

    DrawAddGroupComponentMenu(group, order);
}

// ----------------------------------------------------------------
// Main Render
// ----------------------------------------------------------------

void InspectorPanel::Render(Scene& scene, int selectedEntity, int selectedGroup,
                             int winX, int winY, int panelW, int panelH)
{
    ImGui::SetNextWindowPos(ImVec2((float)winX, (float)winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)panelW, (float)panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove        |
                          ImGuiWindowFlags_NoResize       |
                          ImGuiWindowFlags_NoCollapse     |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Inspector", nullptr, wf);

    if (selectedEntity < 0 && selectedGroup < 0)
    {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::End();
        return;
    }

    // --- Group selected ---
    if (selectedEntity < 0 && selectedGroup >= 0 &&
        selectedGroup < (int)scene.Groups.size())
    {
        DrawGroupSection(scene.Groups[selectedGroup], selectedGroup);
        ImGui::End();
        return;
    }

    if (selectedEntity < 0 || selectedEntity >= (int)scene.EntityNames.size())
    {
        ImGui::TextDisabled("Invalid selection.");
        ImGui::End();
        return;
    }

    // --- Entity name ---
    strncpy(m_NameBuffer, scene.EntityNames[selectedEntity].c_str(),
            sizeof(m_NameBuffer) - 1);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##entname", m_NameBuffer, sizeof(m_NameBuffer),
                         ImGuiInputTextFlags_EnterReturnsTrue))
        scene.EntityNames[selectedEntity] = m_NameBuffer;

    // World position (read-only)
    glm::vec3 wp = scene.GetEntityWorldPos(selectedEntity);
    ImGui::TextDisabled("World: %.2f  %.2f  %.2f", wp.x, wp.y, wp.z);
    ImGui::Separator();

    // --- Transform always first ---
    DrawTransformSection(scene.Transforms[selectedEntity]);

    // --- Other components in user-defined order ---
    std::vector<int>& order = GetOrBuildOrder(scene, selectedEntity);

    for (int i = 0; i < (int)order.size(); /* manual advance */)
    {
        bool kept = true;
        switch (order[i])
        {
            case COMP_CAMERA:
                kept = DrawCameraSection(scene.GameCameras[selectedEntity], i, order);
                break;
            case COMP_GRAVITY:
                kept = DrawGravitySection(scene.RigidBodies[selectedEntity], i, order);
                break;
            case COMP_COLLIDER:
                kept = DrawColliderSection(scene, selectedEntity, i, order);
                break;
        }
        // If removed, the element was erased internally (via GetOrBuildOrder sync next
        // frame), but we still need to skip the increment to handle the new element at i.
        // Since we set the flag false and the erase happens via GetOrBuildOrder on next
        // frame, we just advance normally — the removed state is already set on the
        // component.
        if (!kept)
        {
            // Remove from order immediately so the list stays correct this frame
            order.erase(order.begin() + i);
        }
        else
        {
            ++i;
        }
    }

    // --- Add Component button ---
    DrawAddComponentMenu(scene, selectedEntity, order);

    ImGui::End();
}

} // namespace tsu
