#pragma once
#include "scene/scene.h"
#include "editor/editorCamera.h"

namespace tsu {

class Renderer
{
public:
    static void Init();
    static void BeginFrame();

    // Editor: renderiza scene + gizmos de game cameras
    static void RenderSceneEditor(Scene& scene, const EditorCamera& camera, int winW, int winH);

    // Game: renderiza pela game camera ativa
    static void RenderSceneGame(Scene& scene, int winW, int winH);

    // HUD da barra de play (desenhado por cima, 2D)
    static void RenderToolbar(bool isPlaying, bool isPaused, int winW, int winH,
                               float topOffsetPx = 0.0f);

    // Retorna true se o clique em (x,y) acertou Play ou Pause
    // x,y em pixels da janela
    static bool ToolbarClickPlay (int x, int y, int winW, int winH,
                                  float topOffsetPx = 0.0f);
    static bool ToolbarClickPause(int x, int y, int winW, int winH,
                                  float topOffsetPx = 0.0f);

    // Gizmo de translação 3 eixos no editor
    // activeAxis: 0=none 1=X 2=Y 3=Z
    static void DrawTranslationGizmo(const glm::vec3& pos, int activeAxis,
                                      const EditorCamera& cam, int winW, int winH);

private:
    static unsigned int s_ShaderProgram;
    static unsigned int s_GizmoShader;   // shader simples sem iluminação
    static unsigned int s_HUDShader;     // shader 2D para toolbar

    static void DrawScene(Scene& scene, const glm::mat4& view,
                          const glm::mat4& proj, unsigned int shader);
    static void DrawGizmos(Scene& scene, const glm::mat4& view,
                           const glm::mat4& proj);
    // Wireframe verde dos coliders (ShowCollider = true)
    static void DrawColliderGizmos(Scene& scene, const glm::mat4& view,
                                   const glm::mat4& proj);
    static void InitGizmoMeshes();

    // Shadow map pass (depth-only render from the first active Directional/Spot light)
    static void RenderShadowPass(Scene& scene);

    // Meshes de gizmo (criados uma vez no Init)
    static unsigned int s_GizmoSphereVAO, s_GizmoSphereCount;
    static unsigned int s_GizmoLineVAO,   s_GizmoLineCount;

    // Shadow map resources
    static unsigned int s_ShadowShader;
    static unsigned int s_ShadowFBO;
    static unsigned int s_ShadowDepthTex;
    static glm::mat4    s_ShadowLightSpace;  // lightProj * lightView
    static glm::vec3    s_ShadowLightDir;    // normalised world-space light direction
    static int          s_HasShadow;         // 0 = no shadow caster found this frame
};

} // namespace tsu