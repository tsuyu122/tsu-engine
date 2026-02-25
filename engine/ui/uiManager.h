#pragma once
#include "ui/hierarchyPanel.h"
#include "ui/inspectorPanel.h"

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
    bool Render(Scene& scene, int& selectedEntity, int winW, int winH);
    // Submit ImGui draw data to GPU
    void EndFrame();

    // True when ImGui is using the mouse (prevents editor ray-picking)
    bool WantCaptureMouse() const;

    // Width reserved on each side (pixels) for renderers to know the viewport
    static constexpr int k_PanelWidth = 260;

private:
    HierarchyPanel m_Hierarchy;
    InspectorPanel m_Inspector;
};

} // namespace tsu
