#include "ui/uiManager.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

namespace tsu {

void UIManager::Init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dark theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.ItemSpacing    = ImVec2(6, 4);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.15f, 0.95f);
    style.Colors[ImGuiCol_Header]          = ImVec4(0.25f, 0.40f, 0.70f, 0.6f);
    style.Colors[ImGuiCol_HeaderHovered]   = ImVec4(0.35f, 0.55f, 0.85f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive]    = ImVec4(0.45f, 0.65f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_FrameBg]         = ImVec4(0.20f, 0.20f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_Button]          = ImVec4(0.25f, 0.40f, 0.70f, 0.8f);
    style.Colors[ImGuiCol_ButtonHovered]   = ImVec4(0.35f, 0.55f, 0.85f, 1.0f);
    style.Colors[ImGuiCol_TitleBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.15f, 0.25f, 0.45f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
}

void UIManager::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void UIManager::BeginFrame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

bool UIManager::Render(Scene& scene, int& selectedEntity, int winW, int winH)
{
    // Hierarchy first (updates selectedGroup internally)
    m_Hierarchy.Render(scene, selectedEntity,
                       0, 0, k_PanelWidth, winH);

    int selectedGroup = m_Hierarchy.GetSelectedGroup();

    // Inspector on the right
    m_Inspector.Render(scene, selectedEntity, selectedGroup,
                       winW - k_PanelWidth, 0, k_PanelWidth, winH);

    return ImGui::GetIO().WantCaptureMouse;
}

void UIManager::EndFrame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool UIManager::WantCaptureMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

} // namespace tsu
