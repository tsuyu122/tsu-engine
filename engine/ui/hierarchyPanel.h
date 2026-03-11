#pragma once
#include "scene/scene.h"
#include <string>
#include <functional>

namespace tsu {

class HierarchyPanel
{
public:
    void Render(Scene& scene, int& selectedEntity,
                int winX, int winY, int panelW, int panelH);

    // Prefab editor mode — when active, replaces normal hierarchy with prefab nodes
    void SetPrefabEditorState(bool active, int prefabIdx, int* selectedNodeOut) {
        m_PrefabEditorActive = active;
        m_PrefabEditorIdx    = prefabIdx;
        m_PrefabSelectedNode = selectedNodeOut;
    }
    // Override pointer: when set, use this PrefabAsset instead of scene.Prefabs[idx]
    void SetPrefabOverride(PrefabAsset* p) { m_PrefabOverride = p; }
    // Callbacks set by UIManager so hierarchy buttons can trigger sync/save
    void SetPrefabCallbacks(std::function<void()> syncCb, std::function<void()> saveCb) {
        m_SyncCallback = syncCb;
        m_SaveCallback = saveCb;
    }

    int  GetSelectedGroup()           const { return m_SelectedGroup; }
    const std::vector<int>& GetMultiSelection() const { return m_MultiSelection; }

private:
    void DrawGroupNode (Scene& scene, int groupIdx,
                        int& selectedEntity, int& selectedGroup);
    void DrawEntityNode(Scene& scene, int entityIdx,
                        int& selectedEntity, int& selectedGroup);

    // Prefab editor: draw the node tree
    void DrawPrefabNodeTree(PrefabAsset& prefab, int nodeIdx, int depth);

    // Creates a PrefabAsset from the given entity, saves it to disk, marks entity blue
    void CreatePrefab(Scene& scene, int entityIdx);

    // Returns unique name: "Base", "Base (2)", "Base (3)" ...
    std::string MakeUniqueName(const Scene& scene, const std::string& base);

    // Creates an entity with the given mesh type, starts inline rename
    void CreateEmptyEntity (Scene& scene, int& selectedEntity);
    void CreateMeshEntity  (Scene& scene, const std::string& type, int& selectedEntity);
    void CreateCameraEntity(Scene& scene, int& selectedEntity);
    void CreateLightEntity (Scene& scene, LightType type,           int& selectedEntity);

    int              m_RenamingEntity     = -1;
    int              m_RenamingGroup      = -1;
    int              m_RenamingPrefabNode = -1;
    char             m_RenameBuffer[128] = {};
    char             m_NewGroupName[128] = "Group";
    int              m_SelectedGroup     = -1;
    std::vector<int> m_MultiSelection;
    int              m_LastSelected      = -1;

    // Clipboard for Ctrl+C / Ctrl+V
    std::vector<int> m_ClipboardEntities;  // source entity indices (main scene)
    std::vector<int> m_ClipboardPrefabNodes; // source node indices (prefab/room editor)

    // Prefab editor state (set externally)
    bool             m_PrefabEditorActive = false;
    int              m_PrefabEditorIdx    = -1;
    int*             m_PrefabSelectedNode = nullptr;
    PrefabAsset*     m_PrefabOverride     = nullptr;   // when non-null, use instead of scene.Prefabs[idx]
    std::function<void()> m_SyncCallback;
    std::function<void()> m_SaveCallback;
};

} // namespace tsu

