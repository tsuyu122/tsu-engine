#pragma once
#include "ui/hierarchyPanel.h"
#include "ui/inspectorPanel.h"
#include <vector>
#include <string>
#include <deque>

struct GLFWwindow;

namespace tsu {

class Scene;

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
                std::vector<std::string>& droppedFiles);
    // Submit ImGui draw data to GPU
    void EndFrame();

    // True when ImGui is using the mouse (prevents editor ray-picking)
    bool WantCaptureMouse() const;

    // Viewport tab: 0 = Scene, 1 = Game
    int GetViewportTab()  const { return m_ViewportTab; }
    // Current panel width (resizable by dragging the border)
    int GetPanelWidth()   const { return m_PanelWidth; }

    // Log a message to the console panel
    void Log(const std::string& msg);

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
    int               m_CurrentFolder    = -1;  // -1 = raiz do asset browser
    int               m_ViewportTab      = 0;   // 0=Scene, 1=Game
    int               m_RequestViewportTab = -1;
    bool              m_ProjectSettingsOpen = false;
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
};

} // namespace tsu
