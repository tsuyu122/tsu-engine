#pragma once
#include "scene/scene.h"
#include "core/window.h"
#include "editor/editorCamera.h"
#include "editor/editorGizmo.h"
#include "ui/uiManager.h"

namespace tsu {

enum class EngineMode { Editor, Game, Paused };
enum class GizmoMode  { Move, Rotate, Scale };

class Application
{
public:
    Application();
    void Run();
    Scene&        GetScene()        { return m_Scene; }
    EditorCamera& GetEditorCamera() { return m_EditorCamera; }
    EngineMode    GetMode()   const { return m_Mode; }

    int    GetSelectedEntity() const { return m_SelectedEntity; }
    GizmoMode GetGizmoMode()  const { return m_GizmoMode; }
    void   SetGizmoMode(GizmoMode m) { m_GizmoMode = m; }

private:
    void HandleToolbarInput();
    void HandleEditorInput(int winW, int winH);  // selection + gizmo
    void HandlePrefabEditorInput(int winW, int winH);  // gizmo for prefab preview
    void HandleRoomEditorInput(int winW, int winH);    // gizmo for room preview
    void RebuildPrefabPreview();                        // populate preview scene from prefab
    void SyncPrefabPreviewBack();                       // write preview transforms back to prefab
    void RebuildRoomPreview();                          // populate room preview scene from RoomTemplate
    void SyncRoomPreviewBack();                         // write room preview transforms back to Interior
    void UpdatePlayerControllers(float dt);
    void UpdateMouseLook(float dt);
    void UpdateTriggers(float dt);
    void UpdateAnimators(float dt);
    int  RaycastScene(float mx, float my, int winW, int winH);
    glm::vec3 ComputeViewportSpawnPoint(float mx, float my, int winW, int winH) const;
    void CreateViewportEntity(const std::string& type, int parentEntity, const glm::vec3& worldPos);
    void CreateViewportLight  (LightType type,          int parentEntity, const glm::vec3& worldPos);
    void SaveEditorState();    // saves positions before running
    void RestoreEditorState(); // restaura ao parar

    // ---- Project management ----
    void NewProject(const std::string& name, const std::string& parentFolder);
    void OpenProject(const std::string& path);
    void SaveProject();
    void ExportGame(const std::string& outputFolder, const std::string& gameNameOverride = "");

    // ---- Room Editor Undo/Redo ----
    struct RoomEditState {
        std::vector<RoomBlock>     Blocks;
        std::vector<DoorPlacement> Doors;
    };
    static constexpr int k_MaxRoomHistory = 64;
    std::vector<RoomEditState> m_RoomUndoStack;
    std::vector<RoomEditState> m_RoomRedoStack;
    // Call BEFORE making any block/door change — captures current state
    void PushRoomEditState();
    // Ctrl+Z / Ctrl+Y handlers
    void DoRoomUndo();
    void DoRoomRedo();
    // Helper: returns the RoomTemplate being edited, or nullptr if none
    RoomTemplate* GetEditingRoom();

    Window       m_Window;
    Scene        m_Scene;
    Scene        m_PrefabPreviewScene;   // temp scene for visual prefab editing
    int          m_PrefabPreviewIdx = -1; // which prefab is currently previewed (-1 = none)
    Scene        m_RoomPreviewScene;      // temp scene for 3D room editing
    int          m_RoomPreviewSetIdx = -1;
    int          m_RoomPreviewRoomIdx = -1;
    EditorCamera m_EditorCamera;
    EditorGizmo  m_Gizmo;
    UIManager    m_UIManager;
    EngineMode   m_Mode           = EngineMode::Editor;
    GizmoMode    m_GizmoMode      = GizmoMode::Move;
    int          m_SelectedEntity = -1;
    float        m_LastFrameTime  = 0.0f;

    // Snapshot of positions/velocities to restore when exiting game mode
    struct EntitySnapshot {
        glm::vec3 position, velocity, rotation, angularVelocity;
    };
    std::vector<EntitySnapshot> m_Snapshot;

    // Viewport right-click context menu
    float m_RmbPressX    = 0.0f;
    float m_RmbPressY    = 0.0f;
    int   m_RmbHitEntity = -1;
    glm::vec3 m_RmbWorldPos = glm::vec3(0.0f);
    bool      m_OpenViewportMenu = false;

    // Room editor: per-block exposed face arrows + hover state (rebuilt each frame)
    std::vector<BlockFaceArrow> m_MazeBlockArrows;  // one per exposed block face
    int m_MazeHoveredArrowIdx = -1;  // index into m_MazeBlockArrows; -1=none
    int m_MazeHoveredBlock    = -1;  // index into room.Blocks under cursor

    // ---- Project state ----
    std::string  m_ProjectName   = "Untitled";  // current project name
    std::string  m_ProjectFolder = "";           // absolute path to project root folder ("" = not set)

    // ---- Game export mode ----
    bool         m_GameModeOnly = false;  // true when running as exported standalone game (no editor UI)
};

} // namespace tsu