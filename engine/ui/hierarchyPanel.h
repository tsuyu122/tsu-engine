#pragma once
#include "scene/scene.h"
#include <string>

namespace tsu {

class HierarchyPanel
{
public:
    void Render(Scene& scene, int& selectedEntity,
                int winX, int winY, int panelW, int panelH);

    int  GetSelectedGroup() const { return m_SelectedGroup; }

private:
    void DrawGroupNode (Scene& scene, int groupIdx,
                        int& selectedEntity, int& selectedGroup);
    void DrawEntityNode(Scene& scene, int entityIdx,
                        int& selectedEntity, int& selectedGroup);

    // Returns unique name: "Base", "Base (2)", "Base (3)" ...
    std::string MakeUniqueName(const Scene& scene, const std::string& base);

    // Creates an entity with the given mesh type, starts inline rename
    void CreateMeshEntity(Scene& scene, const std::string& type, int& selectedEntity);
    void CreateCameraEntity(Scene& scene, int& selectedEntity);

    int  m_RenamingEntity  = -1;
    int  m_RenamingGroup   = -1;
    char m_RenameBuffer[128] = {};
    char m_NewGroupName[128] = "Group";
    int  m_SelectedGroup     = -1;
};

} // namespace tsu
