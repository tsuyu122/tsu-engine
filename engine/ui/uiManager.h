#pragma once
#include "ui/hierarchyPanel.h"
#include "ui/inspectorPanel.h"
#include <vector>
#include <string>

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

    // Width reserved on each side (pixels) for renderers to know the viewport
    static constexpr int k_PanelWidth = 260;
    // Height reserved at the bottom for the asset browser
    static constexpr int k_AssetH     = 200;

private:
    HierarchyPanel m_Hierarchy;
    InspectorPanel m_Inspector;
    int            m_SelectedMaterial = -1;
    int            m_SelectedFolder   = -1;
    int            m_CurrentFolder    = -1;  // -1 = raiz do asset browser
};

} // namespace tsu
