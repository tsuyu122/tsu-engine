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

    void Render(Scene& scene, int selectedEntity, int selectedGroup,
                int winX, int winY, int panelW, int panelH,
                int selectedMaterial = -1);

private:
    void DrawTransformSection (TransformComponent& t, const char* label = "Transform");
    // Return false → component was just removed
    bool DrawGravitySection   (RigidBodyComponent& rb, int orderIdx, std::vector<int>& order);
    bool DrawColliderSection  (Scene& scene, int entityIdx,
                               int orderIdx, std::vector<int>& order);
    bool DrawRigidBodySection (RigidBodyComponent& rb, int orderIdx, std::vector<int>& order);
    bool DrawCameraSection    (GameCameraComponent& gc, int orderIdx, std::vector<int>& order);

    void DrawGroupSection     (SceneGroup& group, int groupIdx);
    void DrawAddComponentMenu (Scene& scene, int entityIdx, std::vector<int>& order);
    void DrawAddGroupComponentMenu(SceneGroup& group, std::vector<int>& order);
    void DrawMaterialEditor   (Scene& scene, int matIdx);

    std::vector<int>& GetOrBuildOrder     (Scene& scene, int entityIdx);
    std::vector<int>& GetOrBuildGroupOrder(SceneGroup& group, int groupIdx);

    char m_NameBuffer[128] = {};
    std::map<int, std::vector<int>> m_CompOrder;
    std::map<int, std::vector<int>> m_GroupCompOrder;
};

} // namespace tsu
