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

    // Maze system state
    bool IsMazeWindowOpen()      const { return m_MazeWindowOpen && m_ViewportTab == 4; }
    bool IsMazeRoomEditorOpen()  const { return m_MazeRoomEditorOpen && m_ViewportTab == 5; }
    int  GetEditingRoomSetIdx()  const { return m_EditingRoomSetIdx; }
    int  GetEditingRoomIdx()     const { return m_EditingRoomIdx; }
    int  GetRoomSelectedNode()   const { return m_RoomSelectedNode; }
    void SetRoomSelectedNode(int n)    { m_RoomSelectedNode = n; }
    bool HasPendingRoomClick()   const { return m_PendingRoomClick; }
    glm::vec2 ConsumePendingRoomClick(int& outY) {
        m_PendingRoomClick = false;
        outY = m_PendingRoomClickY;
        return m_PendingRoomClickNDC;
    }
    void CancelPendingRoomClick()      { m_PendingRoomClick = false; }
    // Call when an arrow-click was handled this frame so UIManager won't queue a pending click
    void BlockNextRoomClick()           { m_BlockNextRoomClick = true; }

    // Lightmap bake request (raised by "Bake Lighting" button, consumed by Application)
    bool HasPendingBakeLighting()      const { return m_PendingBakeLighting; }
    void ConsumePendingBakeLighting()        { m_PendingBakeLighting = false; }
    // Written back by Application after successful bake
    void SetBakeStatus(const std::string& s) { m_BakeStatus = s; }
    bool IsDoorMode() const { return m_MazeDoorMode; }
    int  GetRoomEditorLayer()    const { return m_RoomEditorLayer; }
    int  GetMazeSelectedBlock()  const { return m_MazeSelectedBlock; }
    void SetMazeSelectedBlock(int b)   { m_MazeSelectedBlock = b; }
    void OpenMazeWindow()              { m_MazeWindowOpen = true; m_RequestViewportTab = 4; }
    void OpenRoomEditor(int setIdx, int roomIdx);
    void CloseRoomEditor();

    // Log a message to the console panel
    void Log(const std::string& msg);

    // Set the viewport drop world position (computed by Application each frame)
    void SetViewportDropPos(const glm::vec3& pos) { m_ViewportDropPos = pos; }

    // Project operations (raised by File menu, polled by Application)
    bool HasPendingNewProject()  const { return m_PendingNewProject; }
    void ConsumeNewProject(std::string& name, std::string& parentFolder) {
        name = m_DlgProjName; parentFolder = m_DlgProjParent;
        m_PendingNewProject = false;
    }
    bool HasPendingOpenProject() const { return m_PendingOpenProject; }
    void ConsumeOpenProject(std::string& path) {
        path = m_DlgOpenPath;
        m_PendingOpenProject = false;
    }
    bool HasPendingSaveProject() const { return m_PendingSaveProject; }
    void ConsumeSaveProject()          { m_PendingSaveProject = false; }
    bool HasPendingExportGame()  const { return m_PendingExportGame; }
    void ConsumeExportGame(std::string& outputPath, std::string& gameName) {
        outputPath = m_DlgExportPath;
        gameName   = m_DlgGameName;
        m_PendingExportGame = false;
    }
    void SetProjectDisplayName(const std::string& n) { m_ProjectDisplayName = n; }

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
    int               m_ViewportTab      = 0;   // 0=Scene, 1=Game, 2=ProjSettings, 3=PrefabEditor, 4=MazeOverview, 5=RoomEditor
    int               m_RequestViewportTab = -1;
    bool              m_ProjectSettingsOpen = false;
    bool              m_PrefabEditorOpen   = false;
    int               m_EditingPrefabIdx   = -1;
    int               m_PrefabSelectedNode = -1;  // selected node inside prefab editor
    int               m_SelectedChannelIdx = 0;
    // Maze editor state
    bool              m_MazeWindowOpen     = false;
    bool              m_MazeRoomEditorOpen = false;
    int               m_EditingRoomSetIdx  = -1;
    int               m_EditingRoomIdx     = -1;
    int               m_RoomSelectedNode   = -1;   // selected interior node in room editor
    int               m_MazeSelectedBlock  = -1;   // selected block in room editor grid
    bool              m_MazeDoorMode       = false; // when true, clicking places doors instead of blocks
    int               m_RoomEditorLayer    = 0;    // current Y layer for block placement
    PrefabAsset       m_RoomAsPrefab;               // temporary PrefabAsset mirroring room Interior
    // Pending 3D viewport click for room block placement (consumed by Application)
    bool              m_PendingRoomClick    = false;
    bool              m_BlockNextRoomClick  = false;  // set by HandleRoomEditorInput arrow click
    glm::vec2         m_PendingRoomClickNDC = glm::vec2(0.0f);
    int               m_PendingRoomClickY   = 0;
    // Lightmap bake pending flag
    bool              m_PendingBakeLighting = false;
    std::string       m_BakeStatus;          // "" = no status, else e.g. "Baked!" or "Error"
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

    // Project dialog state (File menu)
    bool        m_ShowNewProjectDlg    = false;
    bool        m_ShowOpenProjectDlg   = false;
    bool        m_ShowExportGameDlg    = false;
    bool        m_PendingNewProject    = false;
    bool        m_PendingOpenProject   = false;
    bool        m_PendingSaveProject   = false;
    bool        m_PendingExportGame    = false;
    char        m_DlgProjName[128]     = {};
    char        m_DlgProjParent[512]   = {};
    char        m_DlgOpenPath[512]     = {};
    char        m_DlgExportPath[512]   = {};
    char        m_DlgGameName[256]     = {};
    std::string m_ProjectDisplayName   = "Untitled";
};

} // namespace tsu
