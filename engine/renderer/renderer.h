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

    // Returns true if the click at (x,y) hit Play or Pause
    // x,y in window pixels
    static bool ToolbarClickPlay (int x, int y, int winW, int winH,
                                  float topOffsetPx = 0.0f);
    static bool ToolbarClickPause(int x, int y, int winW, int winH,
                                  float topOffsetPx = 0.0f);

    // 3-axis translation gizmo in the editor
    // activeAxis: 0=none 1=X 2=Y 3=Z 4=XY 5=XZ 6=YZ
    static void DrawTranslationGizmo(const glm::vec3& pos, int activeAxis,
                                      const EditorCamera& cam, int winW, int winH);

    // Rotation gizmo: 3 rings around each axis
    static void DrawRotationGizmo(const glm::vec3& pos, int activeAxis,
                                   const EditorCamera& cam, int winW, int winH);

    // Scale gizmo: 3 axes with cube tips
    static void DrawScaleGizmo(const glm::vec3& pos, int activeAxis,
                                const EditorCamera& cam, int winW, int winH);

private:
    static unsigned int s_ShaderProgram;
    static unsigned int s_GizmoShader;   // simple unlit shader
    static unsigned int s_HUDShader;     // 2D shader for toolbar

    static void DrawScene(Scene& scene, const glm::mat4& view,
                          const glm::mat4& proj, unsigned int shader);
    static void DrawGizmos(Scene& scene, const glm::mat4& view,
                           const glm::mat4& proj);
    // Green wireframe of colliders (ShowCollider = true)
    static void DrawColliderGizmos(Scene& scene, const glm::mat4& view,
                                   const glm::mat4& proj);
    static void InitGizmoMeshes();

    // Shadow map pass (depth-only render from the first active Directional/Spot light)
    static void RenderShadowPass(Scene& scene);

    // Gizmo meshes (created once in Init)
    static unsigned int s_GizmoSphereVAO, s_GizmoSphereCount;
    static unsigned int s_GizmoLineVAO,   s_GizmoLineCount;

    // ---- 2D shadow map (directional / spot) ----
    static unsigned int s_ShadowShader;
    static unsigned int s_ShadowFBO;
    static unsigned int s_ShadowDepthTex;
    static glm::mat4    s_ShadowLightSpace;
    static glm::vec3    s_ShadowLightDir;
    static int          s_HasShadow;
    static int          s_ShadowLightIdx;       // light index [0..7] that casts 2D shadow

    // ---- Point light cubemap shadow ----
    static unsigned int s_PointShadowShader;
    static unsigned int s_PointShadowFBO;
    static unsigned int s_PointShadowCubemap;
    static int          s_HasPointShadow;
    static int          s_PointShadowLightIdx;  // light index [0..7] that casts point shadow
    static glm::vec3    s_PointShadowPos;
    static float        s_PointShadowFar;

    // ---- Cached uniform locations for main PBR shader ----
    struct ShaderLocs {
        int mvp, model, materialColor, texture0, useTexture;
        int shadowMap, normalMap, ormMap;
        int viewPos, hasShadow, lightSpace, shadowDir, shadowLightIdx;
        int hasPointShadow, pointShadowMap, pointShadowPos, pointShadowFar, pointShadowLightIdx;
        int numLights;
        int useNormal, useORM;
        int roughness, metallic, aoValue, tiling, worldSpaceUV;
        struct LightLoc {
            int type, position, direction, color, range, innerCos, outerCos;
        } lights[8];
    };
    static ShaderLocs s_Locs;

    // Per-frame cached world matrices to avoid redundant recursive computation
    static std::vector<glm::mat4> s_CachedWorldMats;
    static void CacheWorldMatrices(Scene& scene);
};

} // namespace tsu