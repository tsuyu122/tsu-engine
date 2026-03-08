#pragma once
#include "scene/scene.h"
#include <map>
#include <vector>

namespace tsu {

class InspectorPanel
{
public:
    // Stable component IDs used in the ordering vector
    static constexpr int COMP_CAMERA     = 0;
    static constexpr int COMP_GRAVITY    = 1;
    static constexpr int COMP_COLLIDER   = 2;
    static constexpr int COMP_RIGIDBODY  = 3;
    static constexpr int COMP_PLAYERCTRL = 4;
    static constexpr int COMP_MOUSELOOK  = 5;
    static constexpr int COMP_LIGHT      = 6;
    static constexpr int COMP_MAZEGEN    = 7;
    static constexpr int COMP_TRIGGER    = 8;
    static constexpr int COMP_ANIMATOR   = 9;
    static constexpr int COMP_LUA        = 10;

    void Render(Scene& scene, int selectedEntity, int selectedGroup,
                int winX, int winY, int panelW, int panelH,
                int selectedMaterial = -1, int selectedTexture = -1,
                int selectedPrefab = -1, const std::vector<int>* multiSelection = nullptr);

    // Prefab editor mode: set before calling Render so inspector shows node properties
    void SetPrefabEditorState(bool active, int prefabIdx, int selectedNode) {
        m_PrefabEditorActive = active;
        m_PrefabEditorIdx    = prefabIdx;
        m_PrefabSelectedNode = selectedNode;
    }
    // Override pointer: when set, use this PrefabAsset instead of scene.Prefabs[idx]
    void SetPrefabOverride(PrefabAsset* p) { m_PrefabOverride = p; }

private:
    void DrawTransformSection (TransformComponent& t, const char* label = "Transform");
    // Return false → component was just removed
    bool DrawGravitySection   (RigidBodyComponent& rb, int orderIdx, std::vector<int>& order);
    bool DrawColliderSection  (Scene& scene, int entityIdx,
                               int orderIdx, std::vector<int>& order);
    bool DrawRigidBodySection (RigidBodyComponent& rb, int orderIdx, std::vector<int>& order);
    bool DrawCameraSection    (GameCameraComponent& gc, Scene& scene,
                                int orderIdx, std::vector<int>& order);
    bool DrawPlayerControllerSection(PlayerControllerComponent& pc,
                                     Scene& scene,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawMouseLookSection       (MouseLookComponent& ml,
                                     Scene& scene,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawLightSection           (LightComponent& lc,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawMazeGeneratorSection   (MazeGeneratorComponent& mg,
                                     Scene& scene,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawTriggerSection         (TriggerComponent& tr,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawAnimatorSection        (AnimatorComponent& an,
                                     int orderIdx,
                                     std::vector<int>& order);
    bool DrawLuaScriptSection       (LuaScriptComponent& ls,
                                     int orderIdx,
                                     std::vector<int>& order);

    // Scene-level settings (Fog, Sky, PostProcess) shown when no entity is selected
    void DrawEnvironmentSettings    (Scene& scene);

    void DrawGroupSection     (SceneGroup& group, int groupIdx);
    void DrawAddComponentMenu (Scene& scene, int entityIdx, std::vector<int>& order);
    void DrawAddGroupComponentMenu(SceneGroup& group, std::vector<int>& order);
    void DrawMaterialEditor   (Scene& scene, int matIdx);
    void DrawTextureInspector (Scene& scene, int texIdx);

    std::vector<int>& GetOrBuildOrder     (Scene& scene, int entityIdx);
    std::vector<int>& GetOrBuildGroupOrder(SceneGroup& group, int groupIdx);

    char m_NameBuffer[128]   = {};
    char m_SearchBuffer[128] = {};  // Add Component search
    Scene*       m_ScenePtr  = nullptr; // set at start of each Render()
    std::map<int, std::vector<int>> m_CompOrder;
    std::map<int, std::vector<int>> m_GroupCompOrder;

    // Prefab editor state (set externally via SetPrefabEditorState)
    bool m_PrefabEditorActive = false;
    int  m_PrefabEditorIdx    = -1;
    int  m_PrefabSelectedNode = -1;
    PrefabAsset* m_PrefabOverride = nullptr;  // when non-null, use instead of scene.Prefabs[idx]
};

} // namespace tsu
