#include "ui/inspectorPanel.h"
#include "input/inputManager.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

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

// ----------------------------------------------------------------
// GLFW key-code ↔ readable name helpers
// ----------------------------------------------------------------

struct KeyNameEntry { int code; const char* name; };
static const KeyNameEntry kKeyNames[] = {
    // Letters
    {65,"A"},{66,"B"},{67,"C"},{68,"D"},{69,"E"},{70,"F"},{71,"G"},
    {72,"H"},{73,"I"},{74,"J"},{75,"K"},{76,"L"},{77,"M"},{78,"N"},
    {79,"O"},{80,"P"},{81,"Q"},{82,"R"},{83,"S"},{84,"T"},{85,"U"},
    {86,"V"},{87,"W"},{88,"X"},{89,"Y"},{90,"Z"},
    // Digits
    {48,"0"},{49,"1"},{50,"2"},{51,"3"},{52,"4"},
    {53,"5"},{54,"6"},{55,"7"},{56,"8"},{57,"9"},
    // F-Keys
    {290,"F1"},{291,"F2"},{292,"F3"},{293,"F4"},{294,"F5"},{295,"F6"},
    {296,"F7"},{297,"F8"},{298,"F9"},{299,"F10"},{300,"F11"},{301,"F12"},
    // Navigation
    {256,"Escape"},{257,"Enter"},{258,"Tab"},{259,"Backspace"},
    {260,"Insert"},{261,"Delete"},{262,"Right"},{263,"Left"},
    {264,"Down"},{265,"Up"},{266,"Page Up"},{267,"Page Down"},
    {268,"Home"},{269,"End"},
    // Modifiers
    {340,"Left Shift"},{341,"Left Ctrl"},{342,"Left Alt"},
    {344,"Right Shift"},{345,"Right Ctrl"},{346,"Right Alt"},
    {343,"Left Super"},{347,"Right Super"},
    // Misc
    {32,"Space"},{39,"Apostrophe"},{44,"Comma"},{45,"Minus"},
    {46,"Period"},{47,"Slash"},{59,"Semicolon"},{61,"Equal"},
    {91,"Left Bracket"},{92,"Backslash"},{93,"Right Bracket"},{96,"Grave"},
    // Numpad
    {320,"Num 0"},{321,"Num 1"},{322,"Num 2"},{323,"Num 3"},{324,"Num 4"},
    {325,"Num 5"},{326,"Num 6"},{327,"Num 7"},{328,"Num 8"},{329,"Num 9"},
    {330,"Num Decimal"},{331,"Num Divide"},{332,"Num Multiply"},
    {333,"Num Subtract"},{334,"Num Add"},{335,"Num Enter"},
    {0,nullptr}
};

static const char* GlfwKeyToName(int code)
{
    for (int i = 0; kKeyNames[i].name; ++i)
        if (kKeyNames[i].code == code) return kKeyNames[i].name;
    return nullptr;
}

static int GlfwKeyFromName(const char* str)
{
    if (!str || !str[0]) return 0;
    for (int i = 0; kKeyNames[i].name; ++i)
    {
        // Case-insensitive compare
        const char* a = kKeyNames[i].name;
        const char* b = str;
        bool eq = true;
        while (*a && *b) {
            if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b)) { eq = false; break; }
            ++a; ++b;
        }
        if (eq && !*a && !*b) return kKeyNames[i].code;
    }
    // Fallback: try parse as integer
    char* end; long v = std::strtol(str, &end, 10);
    if (end != str) return (int)v;
    return 0;
}

// Widget: shows current key name; click to type a new name.
// Internally stores the GLFW key code but displays "W", "Left Shift" etc.
static void KeyNameField(const char* label, int& keyCode)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    // Persistent edit buffer per-ID
    static std::unordered_map<ImGuiID, std::array<char,64>> s_EditBufs;
    static std::unordered_map<ImGuiID, bool>                s_Editing;
    ImGuiID wid = ImGui::GetID("##kf");

    auto& editing = s_Editing[wid];
    auto& buf     = s_EditBufs[wid];

    if (!editing)
    {
        const char* nm = GlfwKeyToName(keyCode);
        char preview[64];
        if (nm) snprintf(preview, sizeof(preview), "%s", nm);
        else    snprintf(preview, sizeof(preview), "%d", keyCode);

        ImGui::SetNextItemWidth(140.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.22f, 0.22f, 0.25f, 1.0f));
        if (ImGui::InputText("##kfdisp", preview, sizeof(preview),
                             ImGuiInputTextFlags_ReadOnly))
        {}
        ImGui::PopStyleColor();
        if (ImGui::IsItemActivated())
        {
            // Enter edit mode
            const char* nm2 = GlfwKeyToName(keyCode);
            if (nm2) snprintf(buf.data(), buf.size(), "%s", nm2);
            else     snprintf(buf.data(), buf.size(), "%d", keyCode);
            editing = true;
        }
    }
    else
    {
        ImGui::SetNextItemWidth(140.0f);
        bool confirmed = ImGui::InputText("##kfedit", buf.data(), buf.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SetKeyboardFocusHere(-1);
        if (confirmed || (!ImGui::IsItemActive() && !ImGui::IsItemFocused()))
        {
            int parsed = GlfwKeyFromName(buf.data());
            if (parsed > 0) keyCode = parsed;
            editing = false;
        }
    }

    // Tooltip showing all available key names (on hover)
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::BeginTooltip();
        ImGui::TextDisabled("Type: W, A, S, D, Space, Left Shift, F1 … or a GLFW code number.");
        ImGui::EndTooltip();
    }

    ImGui::PopID();
}

// ----------------------------------------------------------------
// pEnabled: if non-null, draws a green/grey toggle button to the RIGHT of the header.
//            Mandatory components (Transform, Material) pass nullptr.
static bool DrawCompHeader(const char* label, const char* uid,
                            int orderIdx, std::vector<int>& order,
                            bool& removedOut, bool* pEnabled = nullptr)
{
    removedOut = false;

    // Breathing room between sections
    ImGui::Spacing();
    ImGui::Spacing();

    // Dim the header text when disabled
    if (pEnabled && !(*pEnabled))
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.52f, 0.52f, 0.55f, 1.0f));

    bool open = ImGui::CollapsingHeader(label,
        ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

    if (pEnabled && !(*pEnabled))
        ImGui::PopStyleColor();

    // ---------- Toggle button overlaid on the RIGHT of the header ----------
    if (pEnabled != nullptr)
    {
        float frameH = ImGui::GetFrameHeight();
        float btnW   = frameH * 1.15f;
        ImVec2 hdrMin = ImGui::GetItemRectMin();
        ImVec2 hdrMax = ImGui::GetItemRectMax();
        // Place button at the right edge of the header, vertically centred
        float bx = hdrMax.x - btnW - 4.0f;
        float by = hdrMin.y + (hdrMax.y - hdrMin.y - frameH) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(bx, by));

        ImGui::PushStyleColor(ImGuiCol_Button,
            *pEnabled ? ImVec4(0.20f, 0.72f, 0.26f, 1.0f) : ImVec4(0.38f, 0.38f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            *pEnabled ? ImVec4(0.30f, 0.85f, 0.35f, 1.0f) : ImVec4(0.50f, 0.50f, 0.52f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            *pEnabled ? ImVec4(0.14f, 0.58f, 0.20f, 1.0f) : ImVec4(0.28f, 0.28f, 0.30f, 1.0f));
        char btnId[64]; snprintf(btnId, sizeof(btnId), "##entog_%s", uid);
        if (ImGui::Button(btnId, ImVec2(btnW, frameH)))
            *pEnabled = !(*pEnabled);
        ImGui::PopStyleColor(3);
        // Restore cursor to below the header so content follows normally
        ImGui::SetCursorScreenPos(ImVec2(hdrMin.x, hdrMax.y));
    }

    // Right-click on header → Remove
    char ctxId[64]; snprintf(ctxId, sizeof(ctxId), "##rmctx_%s", uid);
    if (ImGui::BeginPopupContextItem(ctxId))
    {
        if (ImGui::MenuItem("Remove Component")) removedOut = true;
        ImGui::EndPopup();
    }

    // Drag the header itself to reorder
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
// Material module UI (obrigatório, sempre após Transform)
// ----------------------------------------------------------------

static void DrawMaterialModule(Scene& scene, int idx)
{
    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap))
    {
        int matIdx = (idx < (int)scene.EntityMaterial.size()) ? scene.EntityMaterial[idx] : -1;

        // Cor: se nenhum material atribuído, edita EntityColors (local do objeto)
        bool hasMat = (matIdx >= 0 && matIdx < (int)scene.Materials.size());
        glm::vec3 color = hasMat ? scene.Materials[matIdx].Color
                                 : (idx < (int)scene.EntityColors.size() ? scene.EntityColors[idx] : glm::vec3(1.0f));
        ImGui::Text("Cor:");
        if (ImGui::ColorEdit3("##matcolor", &color.x))
        {
            if (hasMat)
                scene.Materials[matIdx].Color = color;
            else if (idx < (int)scene.EntityColors.size())
                scene.EntityColors[idx] = color;
        }

        // Dropdown de material — largura total do painel
        ImGui::Text("Material:");
        ImGui::SetNextItemWidth(-1.0f);
        const char* preview = hasMat ? scene.Materials[matIdx].Name.c_str() : "<Nenhum>";
        if (ImGui::BeginCombo("##matsel", preview, ImGuiComboFlags_PopupAlignLeft))
        {
            if (ImGui::Selectable("<Nenhum>", matIdx == -1))
                scene.EntityMaterial[idx] = -1;
            for (int i = 0; i < (int)scene.Materials.size(); ++i)
            {
                bool sel = (matIdx == i);
                if (ImGui::Selectable(scene.Materials[i].Name.c_str(), sel))
                    scene.EntityMaterial[idx] = i;
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        // Drop target no combo: arrastar material do asset browser
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("MATERIAL_IDX"))
            {
                int newMat = *(const int*)p->Data;
                if (idx < (int)scene.EntityMaterial.size())
                    scene.EntityMaterial[idx] = newMat;
            }
            ImGui::EndDragDropTarget();
        }
    }
}

// ----------------------------------------------------------------
// GetOrBuildOrder – sync stored order with active components
// ----------------------------------------------------------------

std::vector<int>& InspectorPanel::GetOrBuildOrder(Scene& scene, int idx)
{
    std::vector<int>& stored = m_CompOrder[idx];

    auto isActive = [&](int comp) -> bool {
        switch (comp) {
            case COMP_CAMERA:      return scene.GameCameras[idx].Active;
            case COMP_GRAVITY:     return scene.RigidBodies[idx].HasGravityModule;
            case COMP_COLLIDER:    return scene.RigidBodies[idx].HasColliderModule;
            case COMP_RIGIDBODY:   return scene.RigidBodies[idx].HasRigidBodyMode;
            case COMP_PLAYERCTRL:  return scene.PlayerControllers[idx].Active;
            case COMP_MOUSELOOK:   return scene.MouseLooks[idx].Active;
            case COMP_LIGHT:       return scene.Lights[idx].Active;
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
    addMissing(COMP_RIGIDBODY);
    addMissing(COMP_PLAYERCTRL);
    addMissing(COMP_MOUSELOOK);
    addMissing(COMP_LIGHT);

    return stored;
}

// ----------------------------------------------------------------
// Transform section (always shown, no remove)
// ----------------------------------------------------------------

// Helper: labeled DragFloat3 with colored X/Y/Z sub-labels
// defaults[3]: if non-null, double right-click resets each component to its default.
static bool TableDragFloat3XYZ(const char* label, float* v, float speed,
                                float vmin = 0.0f, float vmax = 0.0f,
                                const float* defaults = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);

    bool changed = false;
    float avail = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float fieldW = (avail - spacing * 2.0f - 30.0f) / 3.0f; // 30 = 3*10 for labels

    ImGui::PushID(label);

    // X
    ImGui::TextColored(ImVec4(0.9f,0.25f,0.25f,1), "X");
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::DragFloat("##x", &v[0], speed, vmin, vmax)) changed = true;
    if (defaults && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        { v[0] = defaults[0]; changed = true; }

    ImGui::SameLine(0, spacing);

    // Y
    ImGui::TextColored(ImVec4(0.4f,0.8f,0.2f,1), "Y");
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::DragFloat("##y", &v[1], speed, vmin, vmax)) changed = true;
    if (defaults && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        { v[1] = defaults[1]; changed = true; }

    ImGui::SameLine(0, spacing);

    // Z
    ImGui::TextColored(ImVec4(0.3f,0.5f,0.95f,1), "Z");
    ImGui::SameLine(0, 2);
    ImGui::SetNextItemWidth(fieldW);
    if (ImGui::DragFloat("##z", &v[2], speed, vmin, vmax)) changed = true;
    if (defaults && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        { v[2] = defaults[2]; changed = true; }

    ImGui::PopID();
    return changed;
}

void InspectorPanel::DrawTransformSection(TransformComponent& t, const char* label)
{
    if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (!ImGui::BeginTable("##tf", 2)) return;
    ImGui::TableSetupColumn("lbl", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("val", ImGuiTableColumnFlags_WidthStretch);

    static const float kPosDefault[3] = {0.0f, 0.0f, 0.0f};
    static const float kRotDefault[3] = {0.0f, 0.0f, 0.0f};
    static const float kSclDefault[3] = {1.0f, 1.0f, 1.0f};

    float pos[3] = { t.Position.x, t.Position.y, t.Position.z };
    if (TableDragFloat3XYZ("Position", pos, 0.05f, 0.0f, 0.0f, kPosDefault))
        t.Position = { pos[0], pos[1], pos[2] };

    float rot[3] = { t.Rotation.x, t.Rotation.y, t.Rotation.z };
    if (TableDragFloat3XYZ("Rotation", rot, 0.5f, 0.0f, 0.0f, kRotDefault))
        t.Rotation = { rot[0], rot[1], rot[2] };

    float scl[3] = { t.Scale.x, t.Scale.y, t.Scale.z };
    if (TableDragFloat3XYZ("Scale", scl, 0.01f, 0.001f, 100.0f, kSclDefault))
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
    bool open    = DrawCompHeader("Game Camera", "cam", orderIdx, order, removed, &gc.Enabled);
    if (removed) { gc.Active = false; gc.Enabled = true; return false; }
    if (!open || !gc.Enabled) return true;

    if (!ImGui::BeginTable("##cam", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("FOV",   &gc.FOV,   0.5f, 10.0f, 170.0f);
    TableDragFloat1("Near",  &gc.Near,  0.01f, 0.001f, 10.0f);
    TableDragFloat1("Far",   &gc.Far,   1.0f, 1.0f, 10000.0f);
    bool yawChanged = TableDragFloat1("Yaw",   &gc.Yaw,   0.5f);
    bool pitchChanged = TableDragFloat1("Pitch", &gc.Pitch, 0.5f, -89.0f, 89.0f);
    if (yawChanged || pitchChanged) gc.UpdateVectors();
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
    bool open    = DrawCompHeader("Gravity", "grav", orderIdx, order, removed, &rb.GravityEnabled);
    if (removed) { rb.HasGravityModule = false; rb.GravityEnabled = true; rb.Velocity = {0,0,0}; return false; }
    if (!open || !rb.GravityEnabled) return true;

    ImGui::Checkbox("Use Gravity",  &rb.UseGravity);
    ImGui::Checkbox("Is Kinematic", &rb.IsKinematic);

    if (!ImGui::BeginTable("##grav", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("Mass",      &rb.Mass,               0.05f,  0.001f, 1000.0f);
    TableDragFloat1("Drag",      &rb.Drag,               0.001f, 0.0f,   1.0f);
    TableDragFloat1("FallSpeed", &rb.FallSpeedMultiplier, 0.05f, 0.1f,   10.0f);
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
    bool open    = DrawCompHeader("Collider", "col", orderIdx, order, removed, &rb.ColliderEnabled);
    if (removed) { rb.HasColliderModule = false; rb.ColliderEnabled = true; return false; }
    if (!open || !rb.ColliderEnabled) return true;

    if (rb.HasRigidBodyMode)
    {
        rb.Collider = ColliderType::Box;
        ImGui::BeginDisabled();
        int boxIdx = 1;
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##coltype", &boxIdx, ColliderTypeNames, 5);
        ImGui::EndDisabled();
        ImGui::TextDisabled("RigidBody requires Box collider");
    }
    else
    {
        int colliderIdx = (int)rb.Collider;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##coltype", &colliderIdx, ColliderTypeNames, 5))
            rb.Collider = (ColliderType)colliderIdx;
    }

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

    // Liga/desliga visualização do wireframe verde
    ImGui::Checkbox("Show Collider", &rb.ShowCollider);

    return true;
}

// ----------------------------------------------------------------
// RigidBody module (rotação + impulso + restitução)
// ----------------------------------------------------------------

bool InspectorPanel::DrawRigidBodySection(RigidBodyComponent& rb,
                                           int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("RigidBody", "rb", orderIdx, order, removed, &rb.RigidBodyModeEnabled);
    if (removed) { rb.HasRigidBodyMode = false; rb.RigidBodyModeEnabled = true; rb.AngularVelocity = {0,0,0}; return false; }
    if (!open || !rb.RigidBodyModeEnabled) return true;

    if (!ImGui::BeginTable("##rb", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("Restitution",  &rb.Restitution,   0.01f, 0.0f, 1.0f);
    TableDragFloat1("Friction",     &rb.FrictionCoef,  0.01f, 0.0f, 2.0f);
    TableDragFloat1("AngDamping",   &rb.AngularDamping,0.001f,0.0f, 1.0f);
    ImGui::EndTable();

    ImGui::TextDisabled("AngVel: %.1f  %.1f  %.1f",
        rb.AngularVelocity.x, rb.AngularVelocity.y, rb.AngularVelocity.z);

    if (ImGui::Button("Reset AngVel"))
        rb.AngularVelocity = {0,0,0};

    return true;
}

bool InspectorPanel::DrawPlayerControllerSection(PlayerControllerComponent& pc,
                                                 Scene& scene,
                                                 int orderIdx,
                                                 std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Player Controller", "pc", orderIdx, order, removed, &pc.Enabled);
    if (removed) { pc.Active = false; pc.Enabled = true; pc.IsRunning = false; pc.IsCrouching = false; return false; }
    if (!open || !pc.Enabled) return true;

    int mode = (pc.InputMode == PlayerInputMode::Local) ? 0 : 1;
    if (ImGui::Combo("Input Mode", &mode, "Local\0Channels\0"))
        pc.InputMode = (mode == 0) ? PlayerInputMode::Local : PlayerInputMode::Channels;

    // --- Movement mode combo ---
    // 1=World+Collision  2=Local+Collision  3=World Noclip  4=Local Noclip
    static const char* movModes =
        "1 - World (global axes, collision)\0"
        "2 - Local (facing-relative, collision)\0"
        "3 - World Noclip (global, no collision)\0"
        "4 - Local Noclip (facing-relative, no collision)\0";
    int mm = (int)pc.MovementMode - 1;   // enum 1-4 → combo 0-3
    if (ImGui::Combo("Movement Mode", &mm, movModes))
        pc.MovementMode = (PlayerMovementMode)(mm + 1);

    if (!ImGui::BeginTable("##pc_move", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    TableDragFloat1("Walk Speed", &pc.WalkSpeed, 0.05f, 0.01f, 100.0f);
    TableDragFloat1("Run Mult", &pc.RunMultiplier, 0.05f, 1.0f, 10.0f);
    TableDragFloat1("Crouch Mult", &pc.CrouchMultiplier, 0.05f, 0.05f, 1.0f);
    ImGui::EndTable();

    ImGui::Checkbox("Allow Run", &pc.AllowRun);
    ImGui::SameLine();
    ImGui::Checkbox("Allow Crouch", &pc.AllowCrouch);

    if (pc.InputMode == PlayerInputMode::Local)
    {
        ImGui::SeparatorText("Local Keys");
        KeyNameField("Forward", pc.KeyForward);
        KeyNameField("Back",    pc.KeyBack);
        KeyNameField("Left",    pc.KeyLeft);
        KeyNameField("Right",   pc.KeyRight);
        KeyNameField("Run",     pc.KeyRun);
        KeyNameField("Crouch",  pc.KeyCrouch);
    }
    else
    {
        // Channels mode: each channel stores a String with the key name (e.g. "W").
        // Channels are created/deleted freely in Project Settings.
        ImGui::SeparatorText("Input Channels");

        if (scene.Channels.empty())
        {
            ImGui::TextDisabled("No channels. Create them in Project Settings.");
        }
        else
        {
            // Helper: combo to select a channel index, plus a text input for the key name
            auto channelField = [&](const char* label, int& channelIdx)
            {
                ImGui::PushID(label);

                // --- Channel selector
                const char* chName = (channelIdx >= 0 && channelIdx < (int)scene.Channels.size())
                    ? scene.Channels[channelIdx].Name.c_str() : "<none>";
                ImGui::SetNextItemWidth(130.0f);
                if (ImGui::BeginCombo("##chsel", chName))
                {
                    if (ImGui::Selectable("<none>", channelIdx < 0))
                        channelIdx = -1;
                    for (int k = 0; k < (int)scene.Channels.size(); ++k)
                    {
                        std::string item = std::to_string(k + 1) + " " + scene.Channels[k].Name;
                        bool sel = (channelIdx == k);
                        if (ImGui::Selectable(item.c_str(), sel)) channelIdx = k;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(label);

                // --- Key-name field for the selected channel's StringValue
                if (channelIdx >= 0 && channelIdx < (int)scene.Channels.size())
                {
                    auto& ch = scene.Channels[channelIdx];
                    ch.Type = ChannelVariableType::String;  // enforce String type
                    char buf[64];
                    strncpy(buf, ch.StringValue.c_str(), sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputTextWithHint("##chkey", "Key name e.g. W", buf, sizeof(buf)))
                        ch.StringValue = buf;
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                    {
                        ImGui::BeginTooltip();
                        ImGui::TextDisabled("Type the key name: W, A, Space, Left Shift \xe2\x80\xa6");
                        ImGui::EndTooltip();
                    }
                }

                ImGui::PopID();
            };

            channelField("Forward", pc.ChForward);
            channelField("Back",    pc.ChBack);
            channelField("Left",    pc.ChLeft);
            channelField("Right",   pc.ChRight);
            channelField("Run",     pc.ChRun);
            channelField("Crouch",  pc.ChCrouch);
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Running: %s", pc.IsRunning ? "yes" : "no");
    ImGui::TextDisabled("Crouching: %s", pc.IsCrouching ? "yes" : "no");
    ImGui::TextDisabled("Last Move Axis: %.2f  %.2f  %.2f",
                        pc.LastMoveAxis.x, pc.LastMoveAxis.y, pc.LastMoveAxis.z);

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
    {
        ImGui::OpenPopup("##addcomp");
        m_SearchBuffer[0] = '\0';
    }

    if (ImGui::BeginPopup("##addcomp"))
    {
        // Search field
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##compsearch", "Search components...",
                                 m_SearchBuffer, sizeof(m_SearchBuffer));
        ImGui::Separator();

        const char* q = m_SearchBuffer;
        auto matches = [q](const char* name) -> bool {
            if (q[0] == '\0') return true;
            std::string hay = name, needle = q;
            for (auto& c : hay)    c = (char)std::tolower((unsigned char)c);
            for (auto& c : needle) c = (char)std::tolower((unsigned char)c);
            return hay.find(needle) != std::string::npos;
        };
        const bool searchActive = (q[0] != '\0');
        bool any = false;

        // ---- Camera ----
        bool showCam = !gc.Active && matches("Game Camera");
        if (showCam)
        {
            if (!searchActive)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
                ImGui::TextUnformatted("  Camera");
                ImGui::PopStyleColor();
            }
            if (ImGui::MenuItem("    Game Camera"))
                { gc.Active = true; order.push_back(COMP_CAMERA); }
            any = true;
        }

        // ---- Physics ----
        bool showGrav = !rb.HasGravityModule  && matches("Gravity");
        bool showCol  = !rb.HasColliderModule  && matches("Collider");
        bool showRb   = !rb.HasRigidBodyMode   && matches("RigidBody");
        if (showGrav || showCol || showRb)
        {
            if (!searchActive)
            {
                if (showCam) ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
                ImGui::TextUnformatted("  Physics");
                ImGui::PopStyleColor();
            }
            if (showGrav && ImGui::MenuItem("    Gravity"))
                { rb.HasGravityModule  = true; order.push_back(COMP_GRAVITY); }
            if (showCol  && ImGui::MenuItem("    Collider"))
                { rb.HasColliderModule = true; order.push_back(COMP_COLLIDER); }
            if (showRb   && ImGui::MenuItem("    RigidBody"))
                { rb.HasRigidBodyMode  = true; order.push_back(COMP_RIGIDBODY); }
            any = true;
        }

        // ---- Gameplay ----
        bool showPc = !scene.PlayerControllers[entityIdx].Active && matches("Player Controller");
        bool showMl = !scene.MouseLooks[entityIdx].Active        && matches("Mouse Look");
        if (showPc || showMl)
        {
            if (!searchActive)
            {
                if (showCam || showGrav || showCol || showRb) ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.7f, 1.0f, 1.0f));
                ImGui::TextUnformatted("  Gameplay");
                ImGui::PopStyleColor();
            }
            if (showPc && ImGui::MenuItem("    Player Controller"))
            {
                scene.PlayerControllers[entityIdx].Active = true;
                order.push_back(COMP_PLAYERCTRL);
            }
            if (showMl && ImGui::MenuItem("    Mouse Look"))
            {
                scene.MouseLooks[entityIdx].Active = true;
                order.push_back(COMP_MOUSELOOK);
            }
            any = true;
        }

        // ---- Lighting ----
        bool showLt = !scene.Lights[entityIdx].Active && matches("Light");
        if (showLt)
        {
            if (!searchActive)
            {
                if (showCam || showGrav || showCol || showRb || showPc || showMl) ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.95f, 0.5f, 1.0f));
                ImGui::TextUnformatted("  Lighting");
                ImGui::PopStyleColor();
            }
            if (ImGui::MenuItem("    Directional Light"))
            {
                scene.Lights[entityIdx].Active = true;
                scene.Lights[entityIdx].Type   = LightType::Directional;
                order.push_back(COMP_LIGHT);
            }
            if (ImGui::MenuItem("    Point Light"))
            {
                scene.Lights[entityIdx].Active = true;
                scene.Lights[entityIdx].Type   = LightType::Point;
                scene.Lights[entityIdx].Range  = 10.0f;
                order.push_back(COMP_LIGHT);
            }
            if (ImGui::MenuItem("    Spot Light"))
            {
                scene.Lights[entityIdx].Active      = true;
                scene.Lights[entityIdx].Type        = LightType::Spot;
                scene.Lights[entityIdx].Range       = 15.0f;
                scene.Lights[entityIdx].InnerAngle  = 25.0f;
                scene.Lights[entityIdx].OuterAngle  = 40.0f;
                order.push_back(COMP_LIGHT);
            }
            if (ImGui::MenuItem("    Area Light"))
            {
                scene.Lights[entityIdx].Active  = true;
                scene.Lights[entityIdx].Type    = LightType::Area;
                scene.Lights[entityIdx].Range   = 10.0f;
                scene.Lights[entityIdx].Width   = 2.0f;
                scene.Lights[entityIdx].Height  = 1.0f;
                order.push_back(COMP_LIGHT);
            }
            any = true;
        }

        if (!any)
            ImGui::TextDisabled(searchActive ? "No results for \"%s\"." : "No more components available.", q);

        ImGui::EndPopup();
    }
}

// ----------------------------------------------------------------
// Mouse Look section
// ----------------------------------------------------------------

// Helper: entity drop-slot used in Mouse Look
static void EntityDropSlot(const char* label, int& targetIdx, const Scene& scene)
{
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    char preview[128];
    if (targetIdx >= 0 && targetIdx < (int)scene.EntityNames.size())
        snprintf(preview, sizeof(preview), "%s", scene.EntityNames[targetIdx].c_str());
    else
        snprintf(preview, sizeof(preview), "<None>");

    float bw = ImGui::GetContentRegionAvail().x * 0.65f;
    ImGui::PushStyleColor(ImGuiCol_Button,  ImVec4(0.18f,0.18f,0.21f,1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f,0.38f,0.60f,1.0f));
    ImGui::Button(preview, ImVec2(bw, 0));
    ImGui::PopStyleColor(2);

    // Accept ENTITY drag from hierarchy
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ENTITY"))
            targetIdx = *(const int*)p->Data;
        ImGui::EndDragDropTarget();
    }

    // Right-click to clear
    if (ImGui::BeginPopupContextItem("##slotctx"))
    {
        if (ImGui::MenuItem("Clear")) targetIdx = -1;
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("X##slotx")) targetIdx = -1;

    ImGui::PopID();
}

bool InspectorPanel::DrawMouseLookSection(MouseLookComponent& ml,
                                          Scene& scene,
                                          int orderIdx,
                                          std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Mouse Look", "ml", orderIdx, order, removed, &ml.Enabled);
    if (removed) { ml.Active = false; ml.Enabled = true; ml.CurrentPitch = 0; ml.CurrentYaw = 0; return false; }
    if (!open || !ml.Enabled) return true;

    ImGui::Spacing();
    ImGui::TextDisabled("Drag an entity from Hierarchy onto a slot.");
    EntityDropSlot("Yaw   (Y)", ml.YawTargetEntity,   scene);
    EntityDropSlot("Pitch (X)", ml.PitchTargetEntity, scene);
    ImGui::Spacing();

    if (!ImGui::BeginTable("##ml_tbl", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

    // Sensitivity
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sens X");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##sensx", &ml.SensitivityX, 0.005f, 0.001f, 5.0f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Sens Y");
    ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
    ImGui::DragFloat("##sensy", &ml.SensitivityY, 0.005f, 0.001f, 5.0f);

    ImGui::EndTable();

    ImGui::Checkbox("Invert X", &ml.InvertX); ImGui::SameLine();
    ImGui::Checkbox("Invert Y", &ml.InvertY);
    ImGui::Separator();
    ImGui::Checkbox("Clamp Pitch", &ml.ClampPitch);
    if (ml.ClampPitch)
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::DragFloat("##pmin", &ml.PitchMin, 0.5f, -180.0f, 0.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::DragFloat("##pmax", &ml.PitchMax, 0.5f, 0.0f, 180.0f);
        ImGui::TextDisabled("Pitch clamp: [%.1f, %.1f]", ml.PitchMin, ml.PitchMax);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Pitch: %.2f   Yaw: %.2f", ml.CurrentPitch, ml.CurrentYaw);

    return true;
}

// ----------------------------------------------------------------
// Light section
// ----------------------------------------------------------------

bool InspectorPanel::DrawLightSection(LightComponent& lc, int orderIdx, std::vector<int>& order)
{
    bool removedOut = false;
    bool open = DrawCompHeader("Light", "light_sec", orderIdx, order, removedOut, &lc.Enabled);
    if (removedOut)
    {
        lc.Active  = false;
        lc.Enabled = true;
        return false;
    }
    if (!open) return true;

    if (!ImGui::BeginTable("##lt", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

    // --- Type combo ---
    {
        static const char* typeNames[] = { "Directional", "Point", "Spot", "Area" };
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Type");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        int ti = (int)lc.Type;
        if (ImGui::Combo("##ltype", &ti, typeNames, 4))
            lc.Type = (LightType)ti;
    }

    // --- Color picker ---
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Color");
        ImGui::TableSetColumnIndex(1);
        float col[3] = {lc.Color.r, lc.Color.g, lc.Color.b};
        ImGui::SetNextItemWidth(-1);
        if (ImGui::ColorEdit3("##lcol", col))
            lc.Color = {col[0], col[1], col[2]};
    }

    // --- Temperature with swatch ---
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Temp (K)");
        ImGui::TableSetColumnIndex(1);
        // Approximate preview color from Kelvin
        float t = (lc.Temperature - 1000.0f) / 11000.0f;
        float pr = t < 0.545f ? 1.0f : 1.0f - (t - 0.545f) * 0.55f;
        float pg = t < 0.12f  ? t * 5.0f : (t > 0.727f ? 1.0f - (t - 0.727f)*0.7f : 1.0f);
        float pb = t < 0.545f ? t * 1.65f : 1.0f;
        pr = glm::clamp(pr, 0.0f, 1.0f);
        pg = glm::clamp(pg, 0.0f, 1.0f);
        pb = glm::clamp(pb, 0.0f, 1.0f);

        ImGui::SetNextItemWidth(-38.0f);
        ImGui::DragFloat("##ltemp", &lc.Temperature, 50.0f, 1000.0f, 12000.0f, "%.0f K");
        ImGui::SameLine(0, 4);
        ImGui::ColorButton("##tprev", ImVec4(pr, pg, pb, 1.0f),
            ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
            ImVec2(30.0f, ImGui::GetFrameHeight()));
    }

    // --- Intensity ---
    TableDragFloat1("Intensity", &lc.Intensity, 0.01f, 0.0f, 200.0f);

    // --- Range (not for directional) ---
    if (lc.Type != LightType::Directional)
        TableDragFloat1("Range", &lc.Range, 0.1f, 0.0f, 2000.0f);

    // --- Spot cone angles ---
    if (lc.Type == LightType::Spot)
    {
        TableDragFloat1("Inner \xc2\xb0", &lc.InnerAngle, 0.5f, 0.0f, 89.9f);
        TableDragFloat1("Outer \xc2\xb0", &lc.OuterAngle, 0.5f, 0.0f, 90.0f);
        if (lc.InnerAngle > lc.OuterAngle) lc.InnerAngle = lc.OuterAngle;
    }

    // --- Area light dimensions ---
    if (lc.Type == LightType::Area)
    {
        TableDragFloat1("Width",  &lc.Width,  0.05f, 0.01f, 200.0f);
        TableDragFloat1("Height", &lc.Height, 0.05f, 0.01f, 200.0f);
    }

    ImGui::EndTable();
    return true;
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

// ----------------------------------------------------------------
// Material asset editor (shown when a material is selected in Assets)
// ----------------------------------------------------------------

void InspectorPanel::DrawMaterialEditor(Scene& scene, int matIdx)
{
    if (matIdx < 0 || matIdx >= (int)scene.Materials.size()) return;
    auto& mat = scene.Materials[matIdx];

    ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Material");
    ImGui::Separator();

    // Nome
    static char nameBuf[128];
    strncpy(nameBuf, mat.Name.c_str(), 127);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##matname", nameBuf, 128, ImGuiInputTextFlags_EnterReturnsTrue))
        mat.Name = nameBuf;

    ImGui::Spacing();
    // Cor
    ImGui::Text("Cor:");
    ImGui::ColorEdit3("##matedcolor", &mat.Color.x);

    ImGui::Spacing();
    // Helper: procura TextureID no scene pelo path
    auto lookupTexID = [&](const std::string& path) -> unsigned int {
        auto it = std::find(scene.Textures.begin(), scene.Textures.end(), path);
        if (it == scene.Textures.end()) return 0u;
        int idx = (int)(it - scene.Textures.begin());
        return (idx < (int)scene.TextureIDs.size()) ? scene.TextureIDs[idx] : 0u;
    };
    // Sincroniza TextureID caso o path já tenha sido setado (e.g., load de cena)
    if (!mat.TexturePath.empty() && mat.TextureID == 0)
        mat.TextureID = lookupTexID(mat.TexturePath);

    ImGui::Text("Textura:");

    const float sqSz  = 64.0f;
    float       btnW2 = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f + 6.0f;

    // Quadrado de preview (drag-drop target)
    ImGui::InvisibleButton("##texslot", {sqSz, sqSz});
    {
        ImVec2 sqMin = ImGui::GetItemRectMin();
        ImVec2 sqMax = ImGui::GetItemRectMax();
        auto*  dl    = ImGui::GetWindowDrawList();
        if (mat.TextureID > 0)
            // UV {0,1}->{1,0}: flip V because texture was loaded with flip=true (GL convention)
            dl->AddImage((ImTextureID)(intptr_t)mat.TextureID, sqMin, sqMax, {0,1}, {1,0});
        else {
            dl->AddRectFilled(sqMin, sqMax,
                IM_COL32((int)(mat.Color.r*255),(int)(mat.Color.g*255),(int)(mat.Color.b*255),255), 4.f);
            dl->AddRect      (sqMin, sqMax, IM_COL32(100,100,100,180), 4.f);
        }
    }
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
            mat.TexturePath = std::string((const char*)pl->Data, pl->DataSize - 1);
            mat.TextureID   = lookupTexID(mat.TexturePath);
        }
        ImGui::EndDragDropTarget();
    }

    // Label + botões ao lado do quadrado
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (!mat.TexturePath.empty()) {
        size_t sl = mat.TexturePath.find_last_of("/\\");
        std::string fn = (sl != std::string::npos) ? mat.TexturePath.substr(sl+1) : mat.TexturePath;
        if (fn.size() > 18) fn = fn.substr(0,17) + "\xe2\x80\xa6";
        ImGui::TextUnformatted(fn.c_str());
    } else {
        ImGui::TextDisabled("(nenhuma)");
    }
    if (ImGui::Button("...", {btnW2, 0})) ImGui::OpenPopup("##texturepicker");
    if (!mat.TexturePath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("X", {0,0})) { mat.TexturePath.clear(); mat.TextureID = 0; }
    }
    ImGui::EndGroup();

    if (ImGui::BeginPopup("##texturepicker")) {
        ImGui::Text("Selecionar textura:");
        ImGui::Separator();
        if (scene.Textures.empty()) {
            ImGui::TextDisabled("(nenhuma textura importada)");
        } else {
            for (int ti2 = 0; ti2 < (int)scene.Textures.size(); ++ti2) {
                const auto& tp = scene.Textures[ti2];
                size_t sl2 = tp.find_last_of("/\\");
                std::string fn2 = (sl2 != std::string::npos) ? tp.substr(sl2+1) : tp;
                unsigned int tid = (ti2 < (int)scene.TextureIDs.size()) ? scene.TextureIDs[ti2] : 0u;
                if (tid > 0) { ImGui::Image((ImTextureID)(intptr_t)tid, {24,24}); ImGui::SameLine(); }
                if (ImGui::Selectable(fn2.c_str(), mat.TexturePath == tp)) {
                    mat.TexturePath = tp;
                    mat.TextureID   = tid;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (!mat.TexturePath.empty()) {
            ImGui::Separator();
            if (ImGui::Selectable("(Nenhuma)")) { mat.TexturePath.clear(); mat.TextureID = 0; ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("GL Texture ID: %u", mat.TextureID);
}

void InspectorPanel::Render(Scene& scene, int selectedEntity, int selectedGroup,
                             int winX, int winY, int panelW, int panelH,
                             int selectedMaterial)
{
    ImGui::SetNextWindowPos(ImVec2((float)winX, (float)winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)panelW, (float)panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove        |
                          ImGuiWindowFlags_NoResize       |
                          ImGuiWindowFlags_NoCollapse     |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Inspector", nullptr, wf);

    // Material selecionado no Asset Browser -- mostra editor de material
    if (selectedEntity < 0 && selectedGroup < 0 && selectedMaterial >= 0)
    {
        DrawMaterialEditor(scene, selectedMaterial);
        ImGui::End();
        return;
    }

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


    // --- Material module obrigatório sempre após Transform (exceto para GameCamera) ---
    bool isCamera = (selectedEntity >= 0 && selectedEntity < (int)scene.GameCameras.size() && scene.GameCameras[selectedEntity].Active);
    if (!isCamera)
        DrawMaterialModule(scene, selectedEntity);

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
            case COMP_RIGIDBODY:
                kept = DrawRigidBodySection(scene.RigidBodies[selectedEntity], i, order);
                break;
            case COMP_PLAYERCTRL:
                kept = DrawPlayerControllerSection(scene.PlayerControllers[selectedEntity], scene, i, order);
                break;
            case COMP_MOUSELOOK:
                kept = DrawMouseLookSection(scene.MouseLooks[selectedEntity], scene, i, order);
                break;
            case COMP_LIGHT:
                kept = DrawLightSection(scene.Lights[selectedEntity], i, order);
                break;
        }
        // ...existing code...
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
