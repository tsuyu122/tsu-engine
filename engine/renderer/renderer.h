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

    // Room editor: wireframe cubes for blocks + door markers
    // selectedBlock / hoveredBlock: index into room.Blocks (-1 = none)
    static void DrawRoomBlockWireframes(const RoomTemplate& room,
                                        int selectedBlock, int hoveredBlock,
                                        const EditorCamera& cam,
                                        int winW, int winH);

    // Room editor: per-block face arrows (one per exposed face)
    // shiftMode=true draws them inverted+red (erase mode)
    static void DrawRoomExpansionArrows(const BlockFaceArrow* arrows, int arrowCount,
                                        int hoveredIdx, bool shiftMode,
                                        const EditorCamera& cam,
                                        int winW, int winH);

    // Whether post-processing is applied in the editor viewport.
    // Toggled by the "Post" button in the top toolbar.
    static bool s_EditorPostEnabled;

private:
    static unsigned int s_ShaderProgram;
    static unsigned int s_GizmoShader;   // simple unlit shader
    static unsigned int s_HUDShader;     // 2D shader for toolbar
    static unsigned int s_SkyShader;     // procedural sky (gradient + sun)
    static unsigned int s_PostShader;    // fullscreen post-processing pass
    static unsigned int s_SkyVAO;        // empty VAO for sky fullscreen triangle
    static unsigned int s_ScreenQuadVAO; // empty VAO for post-process fullscreen triangle

    // Post-processing FBO (HDR scene colour + depth renderbuffer)
    static unsigned int s_PostFBO;
    static unsigned int s_PostColorTex;   // GL_RGBA16F scene colour
    static unsigned int s_PostDepthRBO;   // depth renderbuffer
    static int          s_PostW, s_PostH; // current allocated dimensions

    static void DrawScene(Scene& scene, const glm::mat4& view,
                          const glm::mat4& proj, unsigned int shader);
    static void DrawGizmos(Scene& scene, const glm::mat4& view,
                           const glm::mat4& proj);
    // Green wireframe of colliders (ShowCollider = true)
    static void DrawColliderGizmos(Scene& scene, const glm::mat4& view,
                                   const glm::mat4& proj);
    static void InitGizmoMeshes();

    // Procedural sky pass (before opaque; writes depth = 1.0)
    static void DrawSky(const Scene& scene, const glm::mat4& view, const glm::mat4& proj);

    // Post-process composite pass (reads s_PostColorTex → default FBO)
    static void RenderPostProcess(const Scene& scene, int winW, int winH);

    // Ensure post-process FBO matches the (winW, winH) resolution
    static void ResizePostFBO(int winW, int winH);

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
        int useLightmap, lightmapTex, lightmapST;
        // Fog
        int fogType, fogColor, fogDensity, fogStart, fogEnd;
        struct LightLoc {
            int type, position, direction, color, range, innerCos, outerCos;
        } lights[8];
    };
    static ShaderLocs s_Locs;

    // Per-frame cached world matrices to avoid redundant recursive computation
    static std::vector<glm::mat4> s_CachedWorldMats;
    static void CacheWorldMatrices(Scene& scene);

    // ---- Sky shader uniform locations ----
    struct SkyLocs {
        int invProj, invView;
        int zenith, horizon, ground;
        int sunDir, sunColor, sunSize, sunBloom;
    };
    static SkyLocs s_SkyLocs;

    // ---- Post-process shader uniform locations ----
    struct PostLocs {
        int sceneTex, texelSize;
        int bloomEnabled, bloomThreshold, bloomIntensity, bloomRadius;
        int vignetteEnabled, vignetteRadius, vignetteStrength;
        int grainEnabled, grainStrength, grainTime;
        int chromaEnabled, chromaStrength;
        int exposure, saturation, contrast, brightness;
        // New effects
        int sharpenEnabled, sharpenStrength;
        int lensDistortEnabled, lensDistortStrength;
        int sepiaEnabled, sepiaStrength;
        int posterizeEnabled, posterizeLevels;
        int scanlinesEnabled, scanlinesStrength, scanlinesFrequency;
        int pixelateEnabled, pixelateSize;
    };
    static PostLocs s_PostLocs;
};

} // namespace tsu