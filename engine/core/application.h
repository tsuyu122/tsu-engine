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
    void RebuildPrefabPreview();                        // populate preview scene from prefab
    void SyncPrefabPreviewBack();                       // write preview transforms back to prefab
    void UpdatePlayerControllers(float dt);
    void UpdateMouseLook(float dt);
    int  RaycastScene(float mx, float my, int winW, int winH);
    glm::vec3 ComputeViewportSpawnPoint(float mx, float my, int winW, int winH) const;
    void CreateViewportEntity(const std::string& type, int parentEntity, const glm::vec3& worldPos);
    void CreateViewportLight  (LightType type,          int parentEntity, const glm::vec3& worldPos);
    void SaveEditorState();    // saves positions before running
    void RestoreEditorState(); // restaura ao parar

    Window       m_Window;
    Scene        m_Scene;
    Scene        m_PrefabPreviewScene;   // temp scene for visual prefab editing
    int          m_PrefabPreviewIdx = -1; // which prefab is currently previewed (-1 = none)
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
};

} // namespace tsu