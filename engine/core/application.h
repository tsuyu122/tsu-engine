#pragma once
#include "scene/scene.h"
#include "core/window.h"
#include "editor/editorCamera.h"
#include "editor/editorGizmo.h"
#include "ui/uiManager.h"

namespace tsu {

enum class EngineMode { Editor, Game, Paused };

class Application
{
public:
    Application();
    void Run();
    Scene&        GetScene()        { return m_Scene; }
    EditorCamera& GetEditorCamera() { return m_EditorCamera; }
    EngineMode    GetMode()   const { return m_Mode; }

    int    GetSelectedEntity() const { return m_SelectedEntity; }

private:
    void HandleToolbarInput();
    void HandleEditorInput(int winW, int winH);  // seleção + gizmo
    int  RaycastScene(float mx, float my, int winW, int winH);
    void SaveEditorState();    // salva posições antes de rodar
    void RestoreEditorState(); // restaura ao parar

    Window       m_Window;
    Scene        m_Scene;
    EditorCamera m_EditorCamera;
    EditorGizmo  m_Gizmo;
    UIManager    m_UIManager;
    EngineMode   m_Mode           = EngineMode::Editor;
    int          m_SelectedEntity = -1;
    float        m_LastFrameTime  = 0.0f;

    // Snapshot das posições/velocidades para restaurar ao sair do game
    struct EntitySnapshot {
        glm::vec3 position, velocity;
    };
    std::vector<EntitySnapshot> m_Snapshot;
};

} // namespace tsu