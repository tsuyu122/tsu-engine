#pragma once
#include "ui/hierarchyPanel.h"
#include "ui/inspectorPanel.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <deque>

struct GLFWwindow;
struct ImDrawList;

namespace tsu {

class Scene;
enum class GizmoMode;

class UIManager
{
public:
    UIManager() = default;

    void Init(GLFWwindow* window);
    void Shutdown();

    // Call at the start of each frame
    void BeginFrame();
    // Render all panels; returns true if any ImGui window captured input
    bool Render(Scene& scene, int& selectedEntity, int winW, int winH,
                std::vector<std::string>& droppedFiles, GizmoMode& gizmoMode);
    // Submit ImGui draw data to GPU
    void EndFrame();

    // True when ImGui is using the mouse (prevents editor ray-picking)
    bool WantCaptureMouse() const;

    // Viewport tab: 0 = Scene, 1 = Game
    int GetViewportTab()  const { return m_ViewportTab; }
    // Current panel width (resizable by dragging the border)
    int GetPanelWidth()   const { return m_PanelWidth; }
    // Prefab editor state
    bool IsPrefabEditorActive() const { return m_PrefabEditorOpen && m_ViewportTab == 3; }
    int  GetEditingPrefabIdx()  const { return m_EditingPrefabIdx; }
    int  GetPrefabSelectedNode() const { return m_PrefabSelectedNode; }
    void SetPrefabSelectedNode(int n) { m_PrefabSelectedNode = n; }

    // Log a message to the console panel
    void Log(const std::string& msg);

    // Set the viewport drop world position (computed by Application each frame)
    void SetViewportDropPos(const glm::vec3& pos) { m_ViewportDropPos = pos; }

    // Width reserved on each side (pixels) for renderers to know the viewport
    static constexpr int k_PanelWidth = 260;
    static constexpr int k_MenuBarH   = 24;
    static constexpr int k_ToolbarH   = 34;
    static constexpr int k_TopTabsH   = 34;
    static constexpr int k_TopStackH  = k_MenuBarH + k_ToolbarH + k_TopTabsH;
    // Height reserved at the bottom for the asset browser
    static constexpr int k_AssetH     = 200;

private:
    HierarchyPanel    m_Hierarchy;
    InspectorPanel    m_Inspector;
    int               m_SelectedMaterial = -1;
    int               m_SelectedFolder   = -1;
    int               m_SelectedTexture  = -1;
    int               m_SelectedPrefab   = -1;
    int               m_CurrentFolder    = -1;  // -1 = raiz do asset browser
    int               m_ViewportTab      = 0;   // 0=Scene, 1=Game, 2=ProjSettings, 3=PrefabEditor
    int               m_RequestViewportTab = -1;
    bool              m_ProjectSettingsOpen = false;
    bool              m_PrefabEditorOpen   = false;
    int               m_EditingPrefabIdx   = -1;
    int               m_PrefabSelectedNode = -1;  // selected node inside prefab editor
    int               m_SelectedChannelIdx = 0;
    int               m_BottomTab        = 0;   // 0=Assets, 1=Console
    std::deque<std::string> m_ConsoleLogs;
    static constexpr int k_MaxConsoleLogs = 200;
    // Resizable side panels
    int               m_PanelWidth   = k_PanelWidth;
    bool              m_DraggingLeft  = false;
    bool              m_DraggingRight = false;
    // Project settings left-category selection (0 = Channel System)
    int               m_PsCategory   = 0;
    // Viewport drop position (world space, computed by Application)
    glm::vec3         m_ViewportDropPos = glm::vec3(0.0f);
    // Splash screen
    float             m_SplashStart  = -1.0f; // < 0 = not started
    void              DrawSplash(float t, int w, int h);
    // Shared logo draw helper (col = IM_COL32 packed RGBA, i.e. unsigned int)
    static void       DrawEngineLogo(ImDrawList* dl, float cx, float cy,
                                     float sz, unsigned int col,
                                     float ringProgress   = 1.0f,
                                     float eyeAlpha       = 1.0f,
                                     float smileProgress  = 1.0f);
};

} // namespace tsu
