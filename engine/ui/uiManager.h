#pragma once
#include "ui/hierarchyPanel.h"
#include "ui/inspectorPanel.h"
#include "renderer/lightBaker.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <deque>
#include <unordered_map>

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
    // Current panel widths (resizable by dragging borders)
    int GetLeftPanelWidth()  const { return m_LeftPanelWidth; }
    int GetRightPanelWidth() const { return m_RightPanelWidth; }
    int GetPanelWidth()      const { return m_LeftPanelWidth; } // legacy compat
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
    // Clear scene lightmap request
    bool HasPendingClearSceneLightmap()      const { return m_PendingClearSceneLightmap; }
    void ConsumePendingClearSceneLightmap()        { m_PendingClearSceneLightmap = false; }
    // Written back by Application after successful bake
    void SetBakeStatus(const std::string& s) { m_BakeStatus = s; }
    void SetBakeProgress(bool active, float value, const std::string& stage) {
        m_BakeInProgress = active;
        m_BakeProgress = glm::clamp(value, 0.0f, 1.0f);
        m_BakeStage = stage;
    }
    void SetSceneLightmapPath(const std::string& p) { m_SceneLightmapPath = p; }
    const std::string& GetSceneLightmapPath() const  { return m_SceneLightmapPath; }
    // Current bake parameters (edited in Lighting Settings window)
    const LightBaker::BakeParams& GetBakeParams() const { return m_BakeParams; }
    bool IsDoorMode() const { return m_MazeDoorMode; }
    int  GetRoomEditorLayer()    const { return m_RoomEditorLayer; }
    bool GetRoomShowBlocks()     const { return m_RoomShowBlocks; }
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
    void GetExportMetadata(std::string& version, std::string& author, std::string& cover) const {
        version = m_DlgGameVersion;
        author  = m_DlgGameAuthor;
        cover   = m_DlgGameCover;
    }
    bool IsEntityNetHUDEnabled() const { return m_ShowEntityNetHUD; }
    void SetProjectDisplayName(const std::string& n) { m_ProjectDisplayName = n; }

    // Scene management (raised by Scenes panel, polled by Application)
    void SetSceneFiles(const std::vector<std::string>& files, const std::string& currentPath);
    bool HasPendingNewScene()  const { return m_PendingNewScene; }
    void ConsumeNewScene(std::string& name)  { name = m_DlgNewSceneName;  m_PendingNewScene  = false; }
    bool HasPendingOpenScene() const { return m_PendingOpenScene; }
    void ConsumeOpenScene(std::string& path) { path = m_DlgOpenScenePath; m_PendingOpenScene = false; }
    bool HasPendingDeleteScene() const { return m_PendingDeleteScene; }
    void ConsumeDeleteScene(std::string& path) { path = m_DlgDeleteScenePath; m_PendingDeleteScene = false; }

    // Play / Pause toolbar (rendered as ImGui buttons, polled by Application)
    bool HasPendingPlayToggle()  const { return m_PendingPlayToggle; }
    void ConsumePlayToggle()           { m_PendingPlayToggle = false; }
    bool HasPendingPauseToggle() const { return m_PendingPauseToggle; }
    void ConsumePauseToggle()          { m_PendingPauseToggle = false; }
    void SetPlayState(bool playing, bool paused) { m_IsPlaying = playing; m_IsPaused = paused; }
    bool HasPendingLaunchMpHost() const { return m_PendingLaunchMpHost; }
    bool HasPendingLaunchMpClient() const { return m_PendingLaunchMpClient; }
    bool HasPendingLaunchMpPair() const { return m_PendingLaunchMpPair; }
    void ConsumePendingLaunchMpHost(std::string& nick, int& port);
    void ConsumePendingLaunchMpClient(std::string& server, std::string& nick, int& port);
    void ConsumePendingLaunchMpPair(std::string& hostNick, std::string& clientNick, int& port);

    // Width reserved on each side (pixels) for renderers to know the viewport
    static constexpr int k_PanelWidth = 280;
    static constexpr int k_MenuBarH   = 22;
    static constexpr int k_ToolbarH   = 36;
    static constexpr int k_TopTabsH   = 32;
    static constexpr int k_TopStackH  = k_MenuBarH + k_ToolbarH + k_TopTabsH;
    // Height reserved at the bottom for the asset browser
    static constexpr int k_AssetH     = 220;

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
    bool              m_RoomShowBlocks     = true; // show wireframe block outlines in room editor
    PrefabAsset       m_RoomAsPrefab;               // temporary PrefabAsset mirroring room Interior
    // Pending 3D viewport click for room block placement (consumed by Application)
    bool              m_PendingRoomClick    = false;
    bool              m_BlockNextRoomClick  = false;  // set by HandleRoomEditorInput arrow click
    glm::vec2         m_PendingRoomClickNDC = glm::vec2(0.0f);
    int               m_PendingRoomClickY   = 0;
    // Lightmap bake pending flag
    bool              m_PendingBakeLighting = false;
    bool              m_PendingClearSceneLightmap = false;
    std::string       m_BakeStatus;          // "" = no status, else e.g. "Baked!" or "Error"
    std::string       m_SceneLightmapPath;   // path of the scene lightmap (set by Application)
    bool              m_RealtimeWindowOpen = false;
    bool              m_BakedLightingWindowOpen = false;
    bool              m_MultiplayerTestWindowOpen = false;
    bool              m_AnimControllerWindowOpen = false;
    LightBaker::BakeParams m_BakeParams;
    bool              m_BakeInProgress = false;
    float             m_BakeProgress = 0.0f;
    std::string       m_BakeStage;
    int               m_BottomTab        = 0;   // 0=Assets, 1=Console
    std::deque<std::string> m_ConsoleLogs;
    static constexpr int k_MaxConsoleLogs = 200;
    // Resizable side panels (independent left/right)
    int               m_LeftPanelWidth   = k_PanelWidth;
    int               m_RightPanelWidth  = k_PanelWidth;
    bool              m_DraggingLeft  = false;
    bool              m_DraggingRight = false;
    // Panel docking: 0=Hierarchy, 1=Inspector. Value = slot (0=left, 1=right)
    int               m_PanelSlot[2]   = { 0, 1 };  // default: Hierarchy=left, Inspector=right
    bool              m_DraggingPanel  = false;
    int               m_DragPanelId    = -1; // which panel is being dragged (0 or 1)
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
    char        m_DlgGameVersion[64]   = {"1.0.0"};
    char        m_DlgGameAuthor[128]   = {};
    char        m_DlgGameCover[260]    = {"assets/cover.png"};
    bool        m_ShowEntityNetHUD     = false;
    std::string m_ProjectDisplayName   = "Untitled";
    // Play / Pause toolbar state
    bool        m_PendingPlayToggle    = false;
    bool        m_PendingPauseToggle   = false;
    bool        m_IsPlaying            = false;
    bool        m_IsPaused             = false;
    bool        m_PendingLaunchMpHost  = false;
    bool        m_PendingLaunchMpClient= false;
    bool        m_PendingLaunchMpPair  = false;
    char        m_MpServerBuf[128]     = "127.0.0.1";
    char        m_MpHostNickBuf[64]    = "Host";
    char        m_MpClientNickBuf[64]  = "Client";
    int         m_MpPort               = 27015;

    // Scenes panel state
    std::vector<std::string>            m_SceneFiles;
    std::unordered_map<std::string,int> m_SceneFileFolders; // virtual folder per scene path (-1 = root)
    std::string              m_CurrentScenePath;
    bool                     m_PendingNewScene    = false;
    bool                     m_PendingOpenScene   = false;
    bool                     m_PendingDeleteScene = false;
    std::string              m_DlgNewSceneName;
    std::string              m_DlgOpenScenePath;
    std::string              m_DlgDeleteScenePath;
    bool                     m_ShowNewSceneDlg    = false;
    char                     m_NewSceneNameBuf[128] = {};
};

} // namespace tsu
