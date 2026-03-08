#include "ui/inspectorPanel.h"
#include "input/inputManager.h"
#include "renderer/textureLoader.h"
#include <glad/glad.h>
#include <imgui.h>
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <functional>

namespace tsu {

static const char* ColliderTypeNames[] = {
    "None", "Box", "Sphere", "Capsule", "Pyramid"
};

// ----------------------------------------------------------------
// Helper: labeled row in a 2-column table
// ----------------------------------------------------------------

static bool TableDragFloat3(const char* label, float* v, float speed,
                             float vmin = 0.0f, float vmax = 0.0f,
                             const float* def3 = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
    char id[64]; snprintf(id, sizeof(id), "##%s", label);
    bool changed = ImGui::DragFloat3(id, v, speed, vmin, vmax);
    if (def3 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        { v[0]=def3[0]; v[1]=def3[1]; v[2]=def3[2]; changed=true; }
    return changed;
}

static bool TableDragFloat1(const char* label, float* v, float speed,
                             float vmin = 0.0f, float vmax = 0.0f,
                             const float* def1 = nullptr)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1);
    char id[64]; snprintf(id, sizeof(id), "##%s", label);
    bool changed = ImGui::DragFloat(id, v, speed, vmin, vmax);
    if (def1 && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Right))
        { *v = *def1; changed = true; }
    return changed;
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
    static std::unordered_map<ImGuiID, bool>                s_JustOpened;
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
            s_JustOpened[wid] = true;
        }
    }
    else
    {
        ImGui::SetNextItemWidth(140.0f);
        // Focus the text field on the first frame of edit mode
        if (s_JustOpened.count(wid))
        {
            ImGui::SetKeyboardFocusHere(0);
            s_JustOpened.erase(wid);
        }
        bool confirmed = ImGui::InputText("##kfedit", buf.data(), buf.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
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

    // Capture header rect BEFORE any cursor moves
    ImVec2 hdrMin = ImGui::GetItemRectMin();
    ImVec2 hdrMax = ImGui::GetItemRectMax();

    // Right-click on header → Remove  (only for removable components, i.e. pEnabled != nullptr)
    if (pEnabled != nullptr)
    {
        char ctxId[64]; snprintf(ctxId, sizeof(ctxId), "##rmctx_%s", uid);
        if (ImGui::BeginPopupContextItem(ctxId))
        {
            if (ImGui::MenuItem("Remove Component")) removedOut = true;
            ImGui::EndPopup();
        }
    }

    // Drag the header to reorder (also while header is last item)
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

    // ---------- Toggle button + × button overlaid on the RIGHT of the header ----------
    if (pEnabled != nullptr)
    {
        float frameH = ImGui::GetFrameHeight();
        float btnW   = frameH * 1.15f;
        float closeW = frameH * 0.95f;

        // Toggle button (enable/disable)
        float bx = hdrMax.x - btnW - closeW - 8.0f;
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

        // × remove button
        float cx2 = hdrMax.x - closeW - 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(cx2, by));
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.07f, 0.07f, 1.0f));
        char closeId[64]; snprintf(closeId, sizeof(closeId), "##rmx_%s", uid);
        if (ImGui::Button(closeId, ImVec2(closeW, frameH)))
            removedOut = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove component");
        // Draw × text manually centred in the button
        ImVec2 btnPos   = ImGui::GetItemRectMin();
        ImVec2 btnSize  = ImGui::GetItemRectSize();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(btnPos.x + btnSize.x * 0.5f - 4.0f, btnPos.y + btnSize.y * 0.5f - 7.0f),
            IM_COL32(255,220,220,255), "\xc3\x97");  // UTF-8 × (U+00D7)
        ImGui::PopStyleColor(3);

        // Restore cursor to below the header so content follows normally
        ImGui::SetCursorScreenPos(ImVec2(hdrMin.x, hdrMax.y));
    }

    return open;
}

// ----------------------------------------------------------------
// Material module UI (mandatory, always after Transform)
// ----------------------------------------------------------------

static void DrawMaterialModule(Scene& scene, int idx)
{
    ImGui::Spacing();
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap))
    {
        int matIdx = (idx < (int)scene.EntityMaterial.size()) ? scene.EntityMaterial[idx] : -1;

        // Color: if no material assigned, edit EntityColors (per-object)
        bool hasMat = (matIdx >= 0 && matIdx < (int)scene.Materials.size());
        glm::vec3 color = hasMat ? scene.Materials[matIdx].Color
                                 : (idx < (int)scene.EntityColors.size() ? scene.EntityColors[idx] : glm::vec3(1.0f));
        ImGui::Text("Color:");
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
            case COMP_MAZEGEN:     return scene.MazeGenerators[idx].Active;
            case COMP_TRIGGER:     return (idx < (int)scene.Triggers.size()) && scene.Triggers[idx].Active;
            case COMP_ANIMATOR:    return (idx < (int)scene.Animators.size()) && scene.Animators[idx].Active;
            case COMP_LUA:         return (idx < (int)scene.LuaScripts.size()) && scene.LuaScripts[idx].Active;
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
    addMissing(COMP_MAZEGEN);
    addMissing(COMP_TRIGGER);
    addMissing(COMP_ANIMATOR);
    addMissing(COMP_LUA);

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

bool InspectorPanel::DrawCameraSection(GameCameraComponent& gc, Scene& scene,
                                        int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Game Camera", "cam", orderIdx, order, removed, &gc.Enabled);
    if (removed) { gc.Active = false; gc.Enabled = true; return false; }
    if (!open || !gc.Enabled) return true;

    if (!ImGui::BeginTable("##cam", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    static const float kFOVDef=60.f,kNearDef=0.1f,kFarDef=1000.f,kYawDef=0.f,kPitchDef=0.f;
    TableDragFloat1("FOV",   &gc.FOV,   0.5f, 10.0f, 170.0f,   &kFOVDef);
    TableDragFloat1("Near",  &gc.Near,  0.01f, 0.001f, 10.0f,  &kNearDef);
    TableDragFloat1("Far",   &gc.Far,   1.0f, 1.0f, 10000.0f,  &kFarDef);
    bool yawChanged   = TableDragFloat1("Yaw",   &gc.Yaw,   0.5f, 0.f, 0.f, &kYawDef);
    bool pitchChanged = TableDragFloat1("Pitch", &gc.Pitch, 0.5f, -89.0f, 89.0f, &kPitchDef);
    if (yawChanged || pitchChanged) gc.UpdateVectors();
    ImGui::EndTable();

    ImGui::Spacing();
    ImGui::Separator();
    DrawEnvironmentSettings(scene);
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
    static const float kMassDef=1.f,kDragDef=0.f,kFallDef=1.f;
    TableDragFloat1("Mass",      &rb.Mass,               0.05f,  0.001f, 1000.0f, &kMassDef);
    TableDragFloat1("Drag",      &rb.Drag,               0.001f, 0.0f,   1.0f,    &kDragDef);
    TableDragFloat1("FallSpeed", &rb.FallSpeedMultiplier, 0.05f, 0.1f,  10.0f,   &kFallDef);
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
        static const float kSzDef[3]={1.f,1.f,1.f};
        if (TableDragFloat3("Size", sz, 0.01f, 0.001f, 100.0f, kSzDef))
            rb.ColliderSize = { sz[0], sz[1], sz[2] };
    }
    else if (rb.Collider == ColliderType::Sphere)
    {
        static const float kRadDef=0.5f;
        TableDragFloat1("Radius", &rb.ColliderRadius, 0.01f, 0.001f, 100.0f, &kRadDef);
    }
    else if (rb.Collider == ColliderType::Capsule)
    {
        static const float kCapRadDef=0.5f, kCapHDef=2.f;
        TableDragFloat1("Radius", &rb.ColliderRadius, 0.01f, 0.001f, 100.0f, &kCapRadDef);
        TableDragFloat1("Height", &rb.ColliderHeight, 0.01f, 0.001f, 100.0f, &kCapHDef);
    }

    float off[3] = { rb.ColliderOffset.x, rb.ColliderOffset.y, rb.ColliderOffset.z };
    static const float kOffDef[3]={0.f,0.f,0.f};
    if (TableDragFloat3("Offset", off, 0.01f, 0.f, 0.f, kOffDef))
        rb.ColliderOffset = { off[0], off[1], off[2] };

    ImGui::EndTable();

    // Toggle collider wireframe green visualization
    ImGui::Checkbox("Show Collider", &rb.ShowCollider);

    return true;
}

// ----------------------------------------------------------------
// RigidBody module (rotation + impulse + restitution)
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
    static const float kRestDef=0.3f, kFricDef=0.5f, kAngDampDef=0.05f;
    TableDragFloat1("Restitution",  &rb.Restitution,   0.01f, 0.0f, 1.0f,  &kRestDef);
    TableDragFloat1("Friction",     &rb.FrictionCoef,  0.01f, 0.0f, 2.0f,  &kFricDef);
    TableDragFloat1("AngDamping",   &rb.AngularDamping,0.001f,0.0f, 1.0f,  &kAngDampDef);
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
    static const float kWalkDef=5.f, kRunDef=2.f, kCrouchDef=0.5f;
    TableDragFloat1("Walk Speed", &pc.WalkSpeed, 0.05f, 0.01f, 100.0f, &kWalkDef);
    TableDragFloat1("Run Mult", &pc.RunMultiplier, 0.05f, 1.0f, 10.0f, &kRunDef);
    TableDragFloat1("Crouch Mult", &pc.CrouchMultiplier, 0.05f, 0.05f, 1.0f, &kCrouchDef);
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

    // ---- Headbob ----
    ImGui::SeparatorText("Headbob");
    ImGui::Checkbox("Enabled##hb", &pc.HeadbobEnabled);
    if (pc.HeadbobEnabled)
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Camera Entity");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::InputInt("##hbcam", &pc.HeadbobCameraEntity);
        if (pc.HeadbobCameraEntity >= 0 &&
            pc.HeadbobCameraEntity < (int)scene.EntityNames.size())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", scene.EntityNames[pc.HeadbobCameraEntity].c_str());
        }
        ImGui::TextDisabled("Set to the camera child entity index");
        if (ImGui::BeginTable("##hb_tbl", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
            static const float kFreqDef  = 2.2f;
            static const float kAmpYDef  = 0.055f;
            static const float kAmpXDef  = 0.030f;
            TableDragFloat1("Frequency",  &pc.HeadbobFrequency,  0.05f, 0.1f, 10.0f, &kFreqDef);
            TableDragFloat1("Amp Y",      &pc.HeadbobAmplitudeY, 0.002f, 0.0f, 0.5f, &kAmpYDef);
            TableDragFloat1("Amp X",      &pc.HeadbobAmplitudeX, 0.002f, 0.0f, 0.2f, &kAmpXDef);
            ImGui::EndTable();
        }
    }

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

        // ---- Trigger / Animator / Lua Script ----
        bool showTr  = (entityIdx < (int)scene.Triggers.size())  && !scene.Triggers[entityIdx].Active  && matches("Trigger Volume");
        bool showAn  = (entityIdx < (int)scene.Animators.size()) && !scene.Animators[entityIdx].Active && matches("Animator");
        bool showLua = (entityIdx < (int)scene.LuaScripts.size()) && !scene.LuaScripts[entityIdx].Active && matches("Lua");
        if (showTr || showAn || showLua)
        {
            if (!searchActive)
            {
                if (any) ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("  Interaction");
                ImGui::PopStyleColor();
            }
            if (showTr && ImGui::MenuItem("    Trigger Volume"))
            {
                scene.Triggers[entityIdx].Active = true;
                order.push_back(COMP_TRIGGER);
            }
            if (showAn && ImGui::MenuItem("    Animator"))
            {
                scene.Animators[entityIdx].Active  = true;
                scene.Animators[entityIdx].Playing = false;
                order.push_back(COMP_ANIMATOR);
            }
            if (showLua && ImGui::MenuItem("    Lua Script"))
            {
                scene.LuaScripts[entityIdx].Active = true;
                order.push_back(COMP_LUA);
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

        // ---- Procedural ----
        bool showMg = !scene.MazeGenerators[entityIdx].Active && matches("Maze Generator");
        if (showMg)
        {
            if (!searchActive)
            {
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.7f, 1.0f));
                ImGui::TextUnformatted("  Procedural");
                ImGui::PopStyleColor();
            }
            if (ImGui::MenuItem("    Maze Generator"))
            {
                scene.MazeGenerators[entityIdx].Active  = true;
                scene.MazeGenerators[entityIdx].Enabled = true;
                order.push_back(COMP_MAZEGEN);
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
    static const float kIntDef=1.f, kRangeDef=10.f, kInnerDef=25.f, kOuterDef=40.f, kWDef=2.f, kHDef=1.f;
    TableDragFloat1("Intensity", &lc.Intensity, 0.01f, 0.0f, 200.0f, &kIntDef);

    // --- Range (not for directional) ---
    if (lc.Type != LightType::Directional)
        TableDragFloat1("Range", &lc.Range, 0.1f, 0.0f, 2000.0f, &kRangeDef);

    // --- Spot cone angles ---
    if (lc.Type == LightType::Spot)
    {
        TableDragFloat1("Inner \xc2\xb0", &lc.InnerAngle, 0.5f, 0.0f, 89.9f, &kInnerDef);
        TableDragFloat1("Outer \xc2\xb0", &lc.OuterAngle, 0.5f, 0.0f, 90.0f, &kOuterDef);
        if (lc.InnerAngle > lc.OuterAngle) lc.InnerAngle = lc.OuterAngle;
    }

    // --- Area light dimensions ---
    if (lc.Type == LightType::Area)
    {
        TableDragFloat1("Width",  &lc.Width,  0.05f, 0.01f, 200.0f, &kWDef);
        TableDragFloat1("Height", &lc.Height, 0.05f, 0.01f, 200.0f, &kHDef);
    }

    ImGui::EndTable();
    return true;
}

// ----------------------------------------------------------------
// Maze Generator section
// ----------------------------------------------------------------

bool InspectorPanel::DrawMazeGeneratorSection(MazeGeneratorComponent& mg, Scene& scene,
                                               int orderIdx, std::vector<int>& order)
{
    bool removedOut = false;
    bool open = DrawCompHeader("Maze Generator", "mazegen_sec", orderIdx, order, removedOut, &mg.Enabled);
    if (removedOut)
    {
        mg.Active  = false;
        mg.Enabled = true;
        mg.RoomSetIndex = -1;
        mg.SpawnedRooms.clear();
        mg.OccupiedCells.clear();
        return false;
    }
    if (!open) return true;

    if (!ImGui::BeginTable("##mg", 2)) return true;
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);

    // --- Room Set selector ---
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Room Set");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);

        const char* preview = "(None)";
        if (mg.RoomSetIndex >= 0 && mg.RoomSetIndex < (int)scene.RoomSets.size())
            preview = scene.RoomSets[mg.RoomSetIndex].Name.c_str();

        if (ImGui::BeginCombo("##mgset", preview))
        {
            if (ImGui::Selectable("(None)", mg.RoomSetIndex < 0))
                mg.RoomSetIndex = -1;
            for (int si = 0; si < (int)scene.RoomSets.size(); ++si)
            {
                bool sel = (mg.RoomSetIndex == si);
                if (ImGui::Selectable(scene.RoomSets[si].Name.c_str(), sel))
                    mg.RoomSetIndex = si;
            }
            ImGui::EndCombo();
        }
    }

    // --- Generate Radius ---
    TableDragFloat1("Gen Radius", &mg.GenerateRadius, 0.5f, 1.0f, 5000.0f);

    // --- Despawn Radius ---
    TableDragFloat1("Despawn Radius", &mg.DespawnRadius, 0.5f, 1.0f, 5000.0f);

    // --- Block Size ---
    TableDragFloat1("Block Size", &mg.BlockSize, 0.05f, 0.1f, 50.0f);

    // --- Runtime info ---
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("Live Rooms");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%d", (int)mg.SpawnedRooms.size());

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("Occupied Cells");
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%d", (int)mg.OccupiedCells.size());

    ImGui::EndTable();

    // Ensure despawn >= generate
    if (mg.DespawnRadius < mg.GenerateRadius)
        mg.DespawnRadius = mg.GenerateRadius;

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
// Trigger Volume section
// ----------------------------------------------------------------
bool InspectorPanel::DrawTriggerSection(TriggerComponent& tr, int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Trigger Volume", "trig", orderIdx, order, removed, &tr.Enabled);
    if (removed) { tr.Active = false; tr.Enabled = true; tr._PlayerInside = false; tr._HasFired = false; return false; }
    if (!open || !tr.Enabled) return true;

    if (ImGui::BeginTable("##trig_tbl", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        TableDragFloat3("Half-Size", &tr.Size.x,   0.02f, 0.01f, 100.0f);
        TableDragFloat3("Offset",    &tr.Offset.x, 0.01f);
        ImGui::EndTable();
    }

    // Channel
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Channel");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    int ch = tr.Channel;
    if (ImGui::InputInt("##trig_ch", &ch))
        tr.Channel = std::max(-1, ch);
    ImGui::SameLine();
    ImGui::TextDisabled("(-1 = off)");

    if (tr.Channel >= 0)
    {
        ImGui::Checkbox("Value while inside##trig",  &tr.InsideValue);
        ImGui::Checkbox("Value while outside##trig", &tr.OutsideValue);
    }

    ImGui::Checkbox("One Shot##trig",  &tr.OneShot);
    ImGui::Checkbox("Show Trigger##trig", &tr.ShowTrigger);

    // Status
    ImGui::Spacing();
    ImGui::TextColored(
        tr._PlayerInside ? ImVec4(0.2f,1.0f,0.3f,1) : ImVec4(0.45f,0.45f,0.45f,1),
        tr._PlayerInside ? "● INSIDE" : "○ outside");
    if (tr.OneShot && tr._HasFired)
    {
        ImGui::SameLine(0, 12);
        if (ImGui::SmallButton("Reset##trig")) tr._HasFired = false;
    }
    return true;
}

// ----------------------------------------------------------------
// Animator section
// ----------------------------------------------------------------
bool InspectorPanel::DrawAnimatorSection(AnimatorComponent& an, int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Oscillator", "anim", orderIdx, order, removed, &an.Enabled);
    if (removed) { an.Active = false; an.Enabled = true; an.Playing = false; return false; }
    if (!open || !an.Enabled) return true;

    static const char* propNames[] = { "Position", "Rotation", "Scale" };
    static const char* easeNames[] = { "Linear", "Ease In", "Ease Out", "Ease In/Out" };
    static const char* modeNames[] = { "Oscillate", "One Shot", "Loop" };

    if (ImGui::BeginTable("##anim_tbl", 2, ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        // Property combo
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Property");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##animProp", propNames[(int)an.Property]))
        {
            for (int pi = 0; pi < 3; pi++)
                if (ImGui::Selectable(propNames[pi], (int)an.Property == pi))
                    an.Property = (AnimatorProperty)pi;
            ImGui::EndCombo();
        }

        // Mode combo
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Mode");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##animMode", modeNames[(int)an.Mode]))
        {
            for (int mi = 0; mi < 3; mi++)
                if (ImGui::Selectable(modeNames[mi], (int)an.Mode == mi))
                    an.Mode = (AnimatorMode)mi;
            ImGui::EndCombo();
        }

        // Easing combo
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Easing");
        ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##animEase", easeNames[(int)an.Easing]))
        {
            for (int ei = 0; ei < 4; ei++)
                if (ImGui::Selectable(easeNames[ei], (int)an.Easing == ei))
                    an.Easing = (AnimatorEasing)ei;
            ImGui::EndCombo();
        }

        TableDragFloat3("From", &an.From.x, 0.02f);
        TableDragFloat3("To",   &an.To.x,   0.02f);

        static const float defDur = 1.0f;
        TableDragFloat1("Duration", &an.Duration, 0.01f, 0.01f, 60.0f, &defDur);
        TableDragFloat1("Delay",    &an.Delay,    0.01f, 0.0f,  60.0f);

        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Auto Play");
        ImGui::TableSetColumnIndex(1); ImGui::Checkbox("##autoPlay", &an.AutoPlay);

        // Runtime progress
        float prog = (an.Duration > 0.0f) ? std::min(an._Timer / an.Duration, 1.0f) : 0.0f;
        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding(); ImGui::TextDisabled("Progress");
        ImGui::TableSetColumnIndex(1); ImGui::ProgressBar(prog, ImVec2(-1,0));

        ImGui::EndTable();
    }

    // Play / Pause / Reset buttons
    ImGui::Spacing();
    if (!an.Playing)
    {
        if (ImGui::SmallButton("Play")) { an.Playing = true; an._DelayDone = false; an._Timer = 0.0f; an._Dir = 1.0f; }
    }
    else
    {
        if (ImGui::SmallButton("Pause")) an.Playing = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) { an.Playing = false; an._Timer = 0.0f; an._Phase = 0.0f; an._Dir = 1.0f; an._DelayDone = false; }

    return true;
}

// ----------------------------------------------------------------
// Lua Script component section
// ----------------------------------------------------------------
// Parse --@expose annotations from a Lua script into ExposedVars (add new, keep existing user values)
static void SyncExposedVars(LuaScriptComponent& ls)
{
    std::ifstream f(ls.ScriptPath);
    if (!f.is_open()) { ls.ExposedVars.clear(); return; }

    std::map<std::string, std::string> parsed;
    std::string line;
    while (std::getline(f, line)) {
        auto pos = line.find("--@expose ");
        if (pos == std::string::npos) continue;
        std::istringstream ss(line.substr(pos + 10));
        std::string name, second;
        if (!(ss >> name >> second)) continue;
        std::string defaultVal;
        // second may be a type keyword or already the value
        if (second == "number" || second == "string" || second == "boolean") {
            std::getline(ss, defaultVal);
        } else {
            defaultVal = second;
            std::string extra; std::getline(ss, extra);
            if (!extra.empty()) defaultVal += extra;
        }
        // trim
        while (!defaultVal.empty() && (defaultVal.front()==' ' || defaultVal.front()=='\t')) defaultVal.erase(0,1);
        while (!defaultVal.empty() && (defaultVal.back()==' ' || defaultVal.back()=='\r' || defaultVal.back()=='\n')) defaultVal.pop_back();
        parsed[name] = defaultVal;
    }

    // Merge: keep existing user values for known keys, add new keys with defaults
    std::map<std::string, std::string> merged;
    for (auto& [k, v] : parsed)
        merged[k] = ls.ExposedVars.count(k) ? ls.ExposedVars.at(k) : v;
    ls.ExposedVars = std::move(merged);
}

bool InspectorPanel::DrawLuaScriptSection(LuaScriptComponent& ls, int orderIdx, std::vector<int>& order)
{
    bool removed = false;
    bool open    = DrawCompHeader("Lua Script", "lua", orderIdx, order, removed, &ls.Enabled);
    if (removed) { ls.Active = false; ls.Enabled = true; ls.ScriptPath.clear(); ls.ExposedVars.clear(); return false; }
    if (!open || !ls.Enabled) return true;

    ImGui::Spacing();

    // ---- Script path drop zone + path input ----
    static char pathBuf[512] = {};
    strncpy(pathBuf, ls.ScriptPath.c_str(), 511); pathBuf[511] = '\0';

    // Auto-sync if path is set but ExposedVars not yet populated
    if (!ls.ScriptPath.empty() && ls.ExposedVars.empty())
        SyncExposedVars(ls);

    // Styled drop zone frame
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.14f, 0.10f, 1.0f));
    bool pathChanged = ImGui::InputText("##luapath", pathBuf, 512, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();

    // Accept drag-drop from assets panel
    if (ImGui::BeginDragDropTarget()) {
        if (auto* pl = ImGui::AcceptDragDropPayload("SCRIPT_ASSET")) {
            std::string dropped((const char*)pl->Data, pl->DataSize - 1);
            if (ls.ScriptPath != dropped) {
                ls.ScriptPath = dropped;
                pathChanged = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (pathChanged) {
        ls.ScriptPath = pathBuf;
        SyncExposedVars(ls);
    }

    if (ls.ScriptPath.empty()) {
        ImGui::TextDisabled("Drag a .lua file here or type the path");
    } else {
        // Derive file name for display
        size_t sl = ls.ScriptPath.find_last_of("/\\");
        std::string nm = (sl != std::string::npos) ? ls.ScriptPath.substr(sl+1) : ls.ScriptPath;
        ImGui::TextColored(ImVec4(0.4f,1.0f,0.5f,1.0f), "%s", nm.c_str());
    }

    // Sync button
    ImGui::SameLine();
    if (ImGui::SmallButton("Sync##lsync")) SyncExposedVars(ls);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-read --@expose annotations from the script file");

    // ---- Exposed variables ----
    if (!ls.ExposedVars.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Variables");
        ImGui::PushID("##luavars");
        static std::unordered_map<std::string, std::array<char,256>> s_StrBufs;
        for (auto& [k, v] : ls.ExposedVars) {
            // Detect type by value
            bool isBool   = (v == "true" || v == "false");
            bool isNumber = false;
            float numVal  = 0.0f;
            if (!isBool) {
                char* end = nullptr;
                numVal = std::strtof(v.c_str(), &end);
                isNumber = (end && *end == '\0' && end != v.c_str());
            }

            ImGui::PushID(k.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            if (isBool) {
                bool bv = (v == "true");
                if (ImGui::Checkbox(k.c_str(), &bv)) v = bv ? "true" : "false";
            } else if (isNumber) {
                if (ImGui::DragFloat(k.c_str(), &numVal, 0.01f)) {
                    char tmp[64]; snprintf(tmp, 64, "%g", numVal);
                    v = tmp;
                }
            } else {
                auto& buf = s_StrBufs[k];
                strncpy(buf.data(), v.c_str(), 255); buf[255] = '\0';
                if (ImGui::InputText(k.c_str(), buf.data(), 256)) v = buf.data();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    } else if (!ls.ScriptPath.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No --@expose variables found.");
        ImGui::TextDisabled("Add: --@expose myVar number 1.0");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Script functions: OnStart(), OnUpdate(dt), OnStop()");
    ImGui::TextDisabled("Use 'entity_idx' for this entity's index.");

    return true;
}

// ----------------------------------------------------------------
// Environment settings (Fog, Sky, Post-Process) — shown when no entity selected
// ----------------------------------------------------------------
void InspectorPanel::DrawEnvironmentSettings(Scene& scene)
{
    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, 1.0f), "Environment");
    ImGui::Separator();

    // ---- Fog ----
    if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* fogTypeNames[] = { "None", "Linear", "Exponential", "Exp²" };
        int ft = (int)scene.Fog.Type;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##fogType", &ft, fogTypeNames, 4))
            scene.Fog.Type = (FogType)ft;
        if (scene.Fog.Type != FogType::None)
        {
            ImGui::ColorEdit3("Color##fog", &scene.Fog.Color[0]);
            if (scene.Fog.Type == FogType::Linear)
            {
                ImGui::DragFloat("Start##fog", &scene.Fog.Start, 0.5f, 0.0f, 1000.0f);
                ImGui::DragFloat("End##fog",   &scene.Fog.End,   0.5f, 0.0f, 1000.0f);
            }
            else
            {
                ImGui::DragFloat("Density##fog", &scene.Fog.Density, 0.001f, 0.0001f, 1.0f, "%.4f");
            }
        }
    }

    // ---- Sky ----
    if (ImGui::CollapsingHeader("Sky"))
    {
        ImGui::Checkbox("Enabled##sky", &scene.Sky.Enabled);
        if (scene.Sky.Enabled)
        {
            ImGui::ColorEdit3("Zenith##sky",   &scene.Sky.ZenithColor[0]);
            ImGui::ColorEdit3("Horizon##sky",  &scene.Sky.HorizonColor[0]);
            ImGui::ColorEdit3("Ground##sky",   &scene.Sky.GroundColor[0]);
            ImGui::DragFloat3("Sun Dir##sky",  &scene.Sky.SunDirection[0], 0.01f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Sun Color##sky",&scene.Sky.SunColor[0]);
            ImGui::DragFloat("Sun Size##sky",  &scene.Sky.SunSize,  0.001f, 0.001f, 0.5f, "%.3f");
            ImGui::DragFloat("Sun Bloom##sky", &scene.Sky.SunBloom, 0.005f, 0.0f,   0.5f, "%.3f");
        }
    }

    // ---- Post-Process ----
    if (ImGui::CollapsingHeader("Post-Processing"))
    {
        ImGui::Checkbox("Enabled##pp", &scene.PostProcess.Enabled);
        if (scene.PostProcess.Enabled)
        {
            if (ImGui::TreeNodeEx("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("##bloomEn", &scene.PostProcess.BloomEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Bloom");
                if (scene.PostProcess.BloomEnabled)
                {
                    ImGui::DragFloat("Threshold##bloom", &scene.PostProcess.BloomThreshold, 0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("Intensity##bloom",  &scene.PostProcess.BloomIntensity,  0.01f, 0.0f, 5.0f);
                    ImGui::DragFloat("Radius##bloom",     &scene.PostProcess.BloomRadius,     0.1f,  0.1f, 8.0f);
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Vignette", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("##vigEn", &scene.PostProcess.VignetteEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Vignette");
                if (scene.PostProcess.VignetteEnabled)
                {
                    ImGui::DragFloat("Radius##vig",   &scene.PostProcess.VignetteRadius,   0.01f, 0.0f, 1.5f);
                    ImGui::DragFloat("Strength##vig", &scene.PostProcess.VignetteStrength, 0.01f, 0.0f, 1.0f);
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Film Grain"))
            {
                ImGui::Checkbox("##grainEn", &scene.PostProcess.GrainEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Grain");
                if (scene.PostProcess.GrainEnabled)
                    ImGui::DragFloat("Strength##grain", &scene.PostProcess.GrainStrength, 0.001f, 0.0f, 0.5f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Chromatic Aberration"))
            {
                ImGui::Checkbox("##chromaEn", &scene.PostProcess.ChromaEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Chroma");
                if (scene.PostProcess.ChromaEnabled)
                    ImGui::DragFloat("Strength##chroma", &scene.PostProcess.ChromaStrength, 0.0001f, 0.0f, 0.02f, "%.4f");
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Color Grade", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Checkbox("Enabled##cg", &scene.PostProcess.ColorGradeEnabled);
                if (scene.PostProcess.ColorGradeEnabled)
                {
                    ImGui::DragFloat("Exposure##cg",    &scene.PostProcess.Exposure,    0.01f, 0.1f, 5.0f);
                    ImGui::DragFloat("Saturation##cg",  &scene.PostProcess.Saturation,  0.01f, 0.0f, 3.0f);
                    ImGui::DragFloat("Contrast##cg",    &scene.PostProcess.Contrast,    0.01f, 0.0f, 3.0f);
                    ImGui::DragFloat("Brightness##cg",  &scene.PostProcess.Brightness,  0.005f,-0.5f, 0.5f);
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Sharpen"))
            {
                ImGui::Checkbox("##sharpenEn", &scene.PostProcess.SharpenEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Sharpen");
                if (scene.PostProcess.SharpenEnabled)
                    ImGui::DragFloat("Strength##sharpen", &scene.PostProcess.SharpenStrength, 0.05f, 0.0f, 5.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Lens Distortion"))
            {
                ImGui::Checkbox("##lensEn", &scene.PostProcess.LensDistortEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Lens Distortion");
                if (scene.PostProcess.LensDistortEnabled)
                    ImGui::DragFloat("Strength##lens", &scene.PostProcess.LensDistortStrength, 0.01f, -1.0f, 1.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Sepia"))
            {
                ImGui::Checkbox("##sepiaEn", &scene.PostProcess.SepiaEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Sepia");
                if (scene.PostProcess.SepiaEnabled)
                    ImGui::DragFloat("Strength##sepia", &scene.PostProcess.SepiaStrength, 0.01f, 0.0f, 1.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Posterize"))
            {
                ImGui::Checkbox("##posterizeEn", &scene.PostProcess.PosterizeEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Posterize");
                if (scene.PostProcess.PosterizeEnabled)
                    ImGui::DragFloat("Levels##posterize", &scene.PostProcess.PosterizeLevels, 0.5f, 2.0f, 32.0f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Scanlines"))
            {
                ImGui::Checkbox("##scanlinesEn", &scene.PostProcess.ScanlinesEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Scanlines");
                if (scene.PostProcess.ScanlinesEnabled)
                {
                    ImGui::DragFloat("Strength##scanlines",   &scene.PostProcess.ScanlinesStrength,  0.01f, 0.0f, 1.0f);
                    ImGui::DragFloat("Frequency##scanlines",  &scene.PostProcess.ScanlinesFrequency, 50.0f, 50.0f, 4000.0f);
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Pixelate"))
            {
                ImGui::Checkbox("##pixelateEn", &scene.PostProcess.PixelateEnabled);
                ImGui::SameLine(); ImGui::TextUnformatted("Pixelate");
                if (scene.PostProcess.PixelateEnabled)
                    ImGui::DragFloat("Block Size##pixelate", &scene.PostProcess.PixelateSize, 0.5f, 1.0f, 64.0f);
                ImGui::TreePop();
            }
        }
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
                kept = DrawCameraSection(group.GameCamera, *m_ScenePtr, i, order);
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

    ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Material (PBR)");
    ImGui::Separator();

    // Nome
    static char nameBuf[128];
    strncpy(nameBuf, mat.Name.c_str(), 127);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##matname", nameBuf, 128, ImGuiInputTextFlags_EnterReturnsTrue))
        mat.Name = nameBuf;

    ImGui::Spacing();
    ImGui::Text("Tint:");
    ImGui::ColorEdit3("##matedcolor", &mat.Color.x);

    ImGui::Spacing();
    ImGui::Separator();

    // ----------------------------------------------------------------
    // Helper: lookup GL ID by path
    // ----------------------------------------------------------------
    auto lookupTexID = [&](const std::string& path) -> unsigned int {
        auto it = std::find(scene.Textures.begin(), scene.Textures.end(), path);
        if (it == scene.Textures.end()) return 0u;
        int idx = (int)(it - scene.Textures.begin());
        return (idx < (int)scene.TextureIDs.size()) ? scene.TextureIDs[idx] : 0u;
    };

    // ----------------------------------------------------------------
    // Reusable texture slot widget
    // slotLabel    : e.g. "Albedo"
    // pathRef      : std::string& holding the path
    // idRef        : unsigned int& holding the GL texture ID
    // popupId      : unique popup id string
    // isLinear     : load without gamma (normal maps)
    // onChanged    : callback on path change
    // ----------------------------------------------------------------
    auto TextureSlot = [&](const char* slotLabel,
                            std::string& pathRef, unsigned int& idRef,
                            const char* popupId, bool isLinear,
                            std::function<void()> onChanged)
    {
        const float sqSz = 56.0f;
        // Sync ID if path set but ID missing (e.g. after scene load)
        if (!pathRef.empty() && idRef == 0)
            idRef = lookupTexID(pathRef);

        ImGui::PushID(popupId);
        float labelW = ImGui::CalcTextSize(slotLabel).x + 4.0f;
        constexpr float kSlotW = 76.0f; // square + gap

        // Preview square
        ImGui::InvisibleButton("##sq", {sqSz, sqSz});
        {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            auto*  dl = ImGui::GetWindowDrawList();
            if (idRef > 0)
                dl->AddImage((ImTextureID)(intptr_t)idRef, mn, mx, {0,1},{1,0});
            else {
                dl->AddRectFilled(mn, mx, IM_COL32(50,50,55,255), 4.f);
                dl->AddRect(mn, mx, IM_COL32(100,100,110,200), 4.f);
                ImVec2 tc = ImGui::CalcTextSize(slotLabel);
                dl->AddText(ImVec2(mn.x+(sqSz-tc.x)*0.5f, mn.y+(sqSz-tc.y)*0.5f),
                            IM_COL32(130,130,138,200), slotLabel);
            }
        }
        // Drag-drop target on the square
        if (ImGui::BeginDragDropTarget()) {
            if (auto* pl = ImGui::AcceptDragDropPayload("TEXTURE_PATH")) {
                pathRef = std::string((const char*)pl->Data, pl->DataSize - 1);
                idRef   = isLinear ? LoadTextureLinear(pathRef) : lookupTexID(pathRef);
                if (!idRef) idRef = lookupTexID(pathRef);
                onChanged();
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(slotLabel);
        if (!pathRef.empty()) {
            size_t sl = pathRef.find_last_of("/\\");
            std::string fn = (sl != std::string::npos) ? pathRef.substr(sl+1) : pathRef;
            if (fn.size() > 16) fn = fn.substr(0,15) + "\xe2\x80\xa6";
            ImGui::TextDisabled("%s", fn.c_str());
        } else {
            ImGui::TextDisabled("(nenhuma)");
        }
        // Picker button
        if (ImGui::SmallButton("...")) ImGui::OpenPopup(popupId);
        if (!pathRef.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) { pathRef.clear(); idRef = 0; onChanged(); }
        }
        ImGui::EndGroup();

        // Popup
        if (ImGui::BeginPopup(popupId)) {
            ImGui::Text("Selecionar:");
            ImGui::Separator();
            for (int ti2 = 0; ti2 < (int)scene.Textures.size(); ++ti2) {
                const auto& tp = scene.Textures[ti2];
                size_t sl2 = tp.find_last_of("/\\");
                std::string fn2 = (sl2 != std::string::npos) ? tp.substr(sl2+1) : tp;
                unsigned int tid = (ti2 < (int)scene.TextureIDs.size()) ? scene.TextureIDs[ti2] : 0u;
                if (tid > 0) { ImGui::Image((ImTextureID)(intptr_t)tid, {20,20}); ImGui::SameLine(); }
                if (ImGui::Selectable(fn2.c_str(), pathRef == tp)) {
                    pathRef = tp;
                    idRef   = isLinear ? LoadTextureLinear(pathRef) : tid;
                    if (!idRef) idRef = tid;
                    onChanged();
                    ImGui::CloseCurrentPopup();
                }
            }
            if (!pathRef.empty()) {
                ImGui::Separator();
                if (ImGui::Selectable("(Nenhuma)")) { pathRef.clear(); idRef = 0; onChanged(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };

    // ---- Albedo ----
    TextureSlot("Albedo", mat.AlbedoPath, mat.AlbedoID, "##pk_albedo", false, [](){});

    ImGui::Spacing();
    // ---- Normal ----
    TextureSlot("Normal", mat.NormalPath, mat.NormalID, "##pk_normal", true, [](){});

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f,0.9f,1.0f,1), "ORM Sources");
    ImGui::TextDisabled("R=AO  G=Roughness  B=Metallic");
    ImGui::Spacing();

    // ORM-dirty trigger (shared for AO, Roughness, Metallic slots)
    auto markORMDirty = [&]() { mat.ORM_Dirty = true; };

    // ---- Roughness ----
    unsigned int _dummy = 0;
    TextureSlot("Roughness", mat.RoughnessPath, _dummy, "##pk_rough", false, markORMDirty);
    ImGui::Spacing();

    // ---- Metallic ----
    _dummy = 0;
    TextureSlot("Metallic",  mat.MetallicPath,  _dummy, "##pk_metal", false, markORMDirty);
    ImGui::Spacing();

    // ---- AO ----
    _dummy = 0;
    TextureSlot("AO",        mat.AOPath,        _dummy, "##pk_ao",    false, markORMDirty);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f,0.9f,1.0f,1), "Scalar Fallbacks");
    ImGui::TextDisabled("Used when texture is empty  |  RClick = reset");
    ImGui::Spacing();

    // DragFloat with right-click reset helper
    auto PBRDrag = [&](const char* label, float& val, float def,
                        float speed, float lo, float hi, bool& dirty) {
        if (ImGui::DragFloat(label, &val, speed, lo, hi, "%.3f")) dirty = true;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(1)) {
            val = def; dirty = true;
        }
    };

    bool ormDirty = false;
    PBRDrag("Roughness##sc", mat.Roughness, 0.5f, 0.005f, 0.0f, 1.0f, ormDirty);
    PBRDrag("Metallic##sc",  mat.Metallic,  0.0f, 0.005f, 0.0f, 1.0f, ormDirty);
    PBRDrag("AO##sc",        mat.AOValue,   1.0f, 0.005f, 0.0f, 1.0f, ormDirty);
    if (ormDirty) mat.ORM_Dirty = true;

    if (mat.ORM_Dirty)
        ImGui::TextColored(ImVec4(1,0.7f,0.2f,1), "* ORM sera reempacotado no proximo frame");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f,0.9f,1.0f,1), "UV Mapping");
    ImGui::Spacing();
    ImGui::DragFloat2("Tiling##uv", &mat.Tiling[0], 0.05f, 0.01f, 200.0f, "%.2f");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("X: repeticoes nos eixos XZ (horizontal)\nY: repeticoes no eixo Y (vertical)");
    ImGui::Checkbox("World-Space UV", &mat.WorldSpaceUV);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Global grid: adjacent objects align textures\nUseful for floors, walls, wallpaper.");
}

void InspectorPanel::DrawTextureInspector(Scene& scene, int texIdx)
{
    if (texIdx < 0 || texIdx >= (int)scene.Textures.size()) return;
    while ((int)scene.TextureSettings.size() <= texIdx)
        scene.TextureSettings.emplace_back();

    const std::string& fullPath = scene.Textures[texIdx];
    unsigned int glID = (texIdx < (int)scene.TextureIDs.size()) ? scene.TextureIDs[texIdx] : 0;
    auto& cfg = scene.TextureSettings[texIdx];

    size_t sl = fullPath.find_last_of("/\\");
    std::string fname = (sl != std::string::npos) ? fullPath.substr(sl+1) : fullPath;

    ImGui::TextColored(ImVec4(1,0.8f,0.3f,1), "Texture");
    ImGui::Separator();
    ImGui::TextUnformatted(fname.c_str());
    ImGui::TextDisabled("%s", fullPath.c_str());
    ImGui::Spacing();

    if (glID > 0)
        ImGui::Image((ImTextureID)(intptr_t)glID, {128,128}, {0,1},{1,0});

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f,0.9f,1.0f,1), "Import Settings");
    ImGui::Spacing();

    ImGui::Checkbox("Linear (normal/data map)", &cfg.IsLinear);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ligado para normais, roughness, metallic, AO.\nDesligado para albedo (sRGB/cor).");

    const char* wrapModes[] = { "Repeat", "Clamp to Edge", "Mirrored Repeat" };
    bool wrapChanged = false;
    if (ImGui::BeginCombo("Wrap S", wrapModes[cfg.WrapS]))
    {
        for (int w = 0; w < 3; w++) if (ImGui::Selectable(wrapModes[w], cfg.WrapS==w)) { cfg.WrapS=w; wrapChanged=true; }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Wrap T", wrapModes[cfg.WrapT]))
    {
        for (int w = 0; w < 3; w++) if (ImGui::Selectable(wrapModes[w], cfg.WrapT==w)) { cfg.WrapT=w; wrapChanged=true; }
        ImGui::EndCombo();
    }

    const char* filterModes[] = { "Linear + Mipmaps", "Nearest" };
    bool filterChanged = false;
    if (ImGui::BeginCombo("Filter", filterModes[cfg.Filter]))
    {
        for (int f = 0; f < 2; f++) if (ImGui::Selectable(filterModes[f], cfg.Filter==f)) { cfg.Filter=f; filterChanged=true; }
        ImGui::EndCombo();
    }

    bool anisoChanged = ImGui::SliderFloat("Anisotropy", &cfg.Anisotropy, 1.0f, 16.0f, "%.0fx");

    // Apply GL params immediately
    if ((wrapChanged || filterChanged || anisoChanged) && glID > 0)
    {
        static const GLenum wrapGL[] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };
        static const GLint  minGL[]  = { GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST };
        static const GLint  magGL[]  = { GL_LINEAR, GL_NEAREST };
        glBindTexture(GL_TEXTURE_2D, glID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapGL[cfg.WrapS]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapGL[cfg.WrapT]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minGL[cfg.Filter]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magGL[cfg.Filter]);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, cfg.Anisotropy);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void InspectorPanel::Render(Scene& scene, int selectedEntity, int selectedGroup,
                             int winX, int winY, int panelW, int panelH,
                             int selectedMaterial, int selectedTexture,
                             int selectedPrefab, const std::vector<int>* multiSelection)
{
    m_ScenePtr = &scene;   // give helper functions access to the scene this frame

    ImGui::SetNextWindowPos(ImVec2((float)winX, (float)winY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)panelW, (float)panelH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoMove        |
                          ImGuiWindowFlags_NoResize       |
                          ImGuiWindowFlags_NoCollapse     |
                          ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Inspector", nullptr, wf);

    // ---- Multi-select notice ----
    if (multiSelection && multiSelection->size() > 1)
    {
        ImGui::TextDisabled("%d objects selected", (int)multiSelection->size());
        ImGui::Spacing();
        ImGui::TextDisabled("Cannot edit multiple objects simultaneously.");
        ImGui::End();
        return;
    }

    // ---- Prefab editor mode: show selected node properties ----
    if (m_PrefabEditorActive &&
        (m_PrefabOverride || (m_PrefabEditorIdx >= 0 && m_PrefabEditorIdx < (int)scene.Prefabs.size())))
    {
        PrefabAsset& prefab = m_PrefabOverride ? *m_PrefabOverride
                                               : scene.Prefabs[m_PrefabEditorIdx];
        if (m_PrefabSelectedNode >= 0 && m_PrefabSelectedNode < (int)prefab.Nodes.size())
        {
            PrefabEntityData& node = prefab.Nodes[m_PrefabSelectedNode];

            // Editable name
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                               m_PrefabOverride ? "Interior Node" : "Prefab Node");
            ImGui::Separator();
            {
                static char nodeName[128];
                strncpy(nodeName, node.Name.c_str(), 127);
                nodeName[127] = '\0';
                ImGui::Text("Name"); ImGui::SameLine();
                ImGui::SetNextItemWidth(-1);
                if (ImGui::InputText("##prefnodename", nodeName, 128, ImGuiInputTextFlags_EnterReturnsTrue))
                    node.Name = nodeName;
            }

            ImGui::Spacing();

            // Transform — with double-right-click to reset defaults
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
            {
                static const float defPos[3] = {0.0f, 0.0f, 0.0f};
                static const float defRot[3] = {0.0f, 0.0f, 0.0f};
                static const float defScl[3] = {1.0f, 1.0f, 1.0f};
                if (ImGui::BeginTable("##prefnodetfm", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    TableDragFloat3("Position", &node.Transform.Position.x, 0.1f, 0.0f, 0.0f, defPos);
                    TableDragFloat3("Rotation", &node.Transform.Rotation.x, 0.5f, 0.0f, 0.0f, defRot);
                    TableDragFloat3("Scale",    &node.Transform.Scale.x,    0.05f, 0.0f, 0.0f, defScl);
                    ImGui::EndTable();
                }
            }

            // Mesh — editable type dropdown
            {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static const char* meshTypes[] = {"(none)","cube","sphere","plane","pyramid","cylinder","capsule"};
                    int current = 0;
                    for (int i = 1; i < 7; i++)
                        if (node.MeshType == meshTypes[i]) { current = i; break; }
                    ImGui::Text("Type"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##pref_meshtype", &current, meshTypes, 7))
                        node.MeshType = (current == 0) ? "" : meshTypes[current];

                    // Material name editable
                    static char matBuf[128];
                    strncpy(matBuf, node.MaterialName.c_str(), 127); matBuf[127] = '\0';
                    ImGui::Text("Material"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::InputText("##pref_matname", matBuf, 128, ImGuiInputTextFlags_EnterReturnsTrue))
                        node.MaterialName = matBuf;
                }
            }

            // Light
            if (node.HasLight)
            {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static const char* ltNames[] = {"Directional","Point","Spot","Area"};
                    int ltIdx = (int)node.Light.Type;
                    ImGui::Text("Type"); ImGui::SameLine();
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##pref_lighttype", &ltIdx, ltNames, 4))
                        node.Light.Type = (LightType)ltIdx;
                    ImGui::DragFloat("Intensity", &node.Light.Intensity, 0.05f, 0.0f, 100.0f);
                    ImGui::ColorEdit3("Color", &node.Light.Color.x);
                    if (node.Light.Type == LightType::Point || node.Light.Type == LightType::Spot)
                        ImGui::DragFloat("Range", &node.Light.Range, 0.1f, 0.0f, 1000.0f);
                    if (node.Light.Type == LightType::Spot) {
                        ImGui::DragFloat("Inner Angle", &node.Light.InnerAngle, 0.5f, 0.0f, 90.0f);
                        ImGui::DragFloat("Outer Angle", &node.Light.OuterAngle, 0.5f, 0.0f, 90.0f);
                    }
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Remove Light", ImVec2(-1, 0)))
                        node.HasLight = false;
                    ImGui::PopStyleColor();
                }
            }

            // Camera
            if (node.HasCamera)
            {
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::DragFloat("FOV", &node.Camera.FOV, 0.5f, 1.0f, 179.0f);
                    ImGui::DragFloat("Near", &node.Camera.Near, 0.01f, 0.001f, 100.0f);
                    ImGui::DragFloat("Far", &node.Camera.Far, 1.0f, 1.0f, 100000.0f);
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Remove Camera", ImVec2(-1, 0)))
                        node.HasCamera = false;
                    ImGui::PopStyleColor();
                }
            }

            // Add component buttons
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (!node.HasLight) {
                if (ImGui::Button("Add Light", ImVec2(-1, 0))) {
                    node.HasLight       = true;
                    node.Light.Active   = true;
                    node.Light.Enabled  = true;
                    node.Light.Type     = LightType::Point;
                    node.Light.Intensity= 1.0f;
                    node.Light.Color    = glm::vec3(1.0f);
                    node.Light.Range    = 10.0f;
                }
            }
            if (!node.HasCamera) {
                if (ImGui::Button("Add Camera", ImVec2(-1, 0))) {
                    node.HasCamera     = true;
                    node.Camera.Active = true;
                    node.Camera.FOV    = 60.0f;
                    node.Camera.Near   = 0.1f;
                    node.Camera.Far    = 1000.0f;
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Select a node in the hierarchy to edit.");
        }
        ImGui::End();
        return;
    }

    // ---- Prefab selected in Asset Browser ----
    if (selectedEntity < 0 && selectedGroup < 0 &&
        selectedPrefab >= 0 && selectedPrefab < (int)scene.Prefabs.size())
    {
        const PrefabAsset& p = scene.Prefabs[selectedPrefab];
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Prefab");
        ImGui::Separator();
        ImGui::Text("Name:  %s", p.Name.c_str());
        ImGui::Text("Nodes: %d", (int)p.Nodes.size());
        if (!p.Nodes.empty())
        {
            const PrefabEntityData& root = p.Nodes[0];
            if (!root.MeshType.empty())
                ImGui::Text("Root mesh: %s", root.MeshType.c_str());
            if (!root.MaterialName.empty())
                ImGui::Text("Material:  %s", root.MaterialName.c_str());
            if (root.HasLight)
            {
                static const char* ltN[] = {"Directional","Point","Spot","Area"};
                ImGui::Text("Light: %s  int=%.2f", ltN[(int)root.Light.Type], root.Light.Intensity);
            }
            if (root.HasCamera)
                ImGui::Text("Camera  FOV=%.1f", root.Camera.FOV);
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Double-click in Assets to instantiate.");
        ImGui::End();
        return;
    }

    // Texture selected in Asset Browser
    if (selectedEntity < 0 && selectedGroup < 0 && selectedTexture >= 0)
    {
        DrawTextureInspector(scene, selectedTexture);
        ImGui::End();
        return;
    }

    // Material selected in Asset Browser -- show material editor
    if (selectedEntity < 0 && selectedGroup < 0 && selectedMaterial >= 0)
    {
        DrawMaterialEditor(scene, selectedMaterial);
        ImGui::End();
        return;
    }

    if (selectedEntity < 0 && selectedGroup < 0)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("Select an entity to inspect its components.");
        ImGui::Spacing();
        ImGui::TextDisabled("Environment settings (Fog / Sky / Post-Processing)");
        ImGui::TextDisabled("are accessible from the Game Camera component.");
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


    // --- Material module mandatory always after Transform (except for GameCamera) ---
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
                kept = DrawCameraSection(scene.GameCameras[selectedEntity], scene, i, order);
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
            case COMP_MAZEGEN:
                kept = DrawMazeGeneratorSection(scene.MazeGenerators[selectedEntity], scene, i, order);
                break;
            case COMP_TRIGGER:
                if (selectedEntity < (int)scene.Triggers.size())
                    kept = DrawTriggerSection(scene.Triggers[selectedEntity], i, order);
                break;
            case COMP_ANIMATOR:
                if (selectedEntity < (int)scene.Animators.size())
                    kept = DrawAnimatorSection(scene.Animators[selectedEntity], i, order);
                break;
            case COMP_LUA:
                if (selectedEntity < (int)scene.LuaScripts.size())
                    kept = DrawLuaScriptSection(scene.LuaScripts[selectedEntity], i, order);
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
