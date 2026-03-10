#pragma once
#include "scene/scene.h"
#include "renderer/mesh.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>

namespace tsu {

// CPU-side lightmap baker for room templates.
// Produces a top-down RGB ambient-occlusion + direct-light texture.
//
// World-XZ → UV mapping stored in RoomTemplate::LightmapST:
//   u = worldX * ST.x + ST.z
//   v = worldZ * ST.y + ST.w
class LightBaker
{
public:
    struct BakeParams {
        int   resolution    = 512;   // output texture size (square)
        int   aoSamples     = 64;    // hemisphere samples for AO
        int   indirectSamples = 64;  // hemisphere samples for indirect GI
        int   bounceCount   = 2;     // simplified GI bounce count
        int   denoiseRadius = 1;     // post-filter radius in texels
        float aoRadius      = 4.0f;  // max AO occlusion distance (blocks)
        float ambientLevel  = 0.08f; // minimum ambient floor (0..1)
        float directScale   = 1.20f; // direct light contribution scale
    };

    // Flattened per-light data (precomputed from scene; avoids baking depending on Scene)
    struct LightInfo {
        LightType type;
        glm::vec3 color;      // Color * Intensity (kelvin filter already applied)
        float     range;
        float     innerAngle; // spot: degrees
        float     outerAngle; // spot: degrees
        glm::vec3 position;   // world position (point / spot)
        glm::vec3 direction;  // world-space forward (dir / spot = wm[0] column)
    };

    struct BakeResult {
        int   width = 0, height = 0;
        std::vector<unsigned char> pixels; // RGB8, row-major top-left
        glm::vec4 lightmapST;             // converts world-XZ → [0,1] UV
    };

    struct SceneBakeResult {
        int width = 0, height = 0;
        std::vector<unsigned char> pixelsX; // projection X (sample world YZ)
        std::vector<unsigned char> pixelsY; // projection Y (sample world XZ)
        std::vector<unsigned char> pixelsZ; // projection Z (sample world XY)
        glm::vec4 stX = {1,1,0,0};
        glm::vec4 stY = {1,1,0,0};
        glm::vec4 stZ = {1,1,0,0};
    };

    struct AtlasAssignment {
        int entity = -1;
        int lightmapIndex = 0;
        glm::vec4 scaleOffset = {1,1,0,0};
    };

    struct AtlasBakeResult {
        int width = 0, height = 0;
        std::vector<unsigned char> pixels; // RGB8 atlas (gamma-encoded for TGA preview)
        std::vector<float> hdrPixels;     // Linear HDR float RGB (for GL_RGB16F upload)
        std::vector<AtlasAssignment> assignments;
        float hdrMax = 1.0f; // peak HDR value before normalization (use as LightmapIntensity)
    };

    // Bake the room. 'lights' can be built with ExtractLights().
    static BakeResult BakeRoom(const RoomTemplate& room,
                                const std::vector<LightInfo>& lights,
                                const BakeParams& params = BakeParams());

    // Build LightInfo list from the current scene (call from UI/Application).
    static std::vector<LightInfo> ExtractLights(const Scene& scene);

    // Build LightInfo list from room Interior nodes (point/spot lights inside the room).
    static std::vector<LightInfo> ExtractRoomLights(const RoomTemplate& room);

    // Bake the main scene entities (standalone objects, not maze/room).
    // Uses world-space AABBs derived from entity transforms for occlusion.
    static BakeResult BakeScene(const Scene& scene,
                                 const std::vector<LightInfo>& lights,
                                 const BakeParams& params = BakeParams());

    // Tri-planar scene bake for per-face style application.
    static SceneBakeResult BakeSceneTriPlanar(const Scene& scene,
                                              const std::vector<LightInfo>& lights,
                                              const BakeParams& params = BakeParams());

    // UV2 atlas bake (Unity-style flow: triangle charts -> atlas -> per-texel shading).
    static AtlasBakeResult BakeSceneAtlasUV2(
        Scene& scene,
        const std::vector<LightInfo>& lights,
        const BakeParams& params = BakeParams(),
        const std::function<void(float, const std::string&)>& progress = {});

    // Bake, save TGA, return the saved path ("" on failure).
    static std::string BakeAndSave(const RoomTemplate& room,
                                    const std::vector<LightInfo>& lights,
                                    const std::string& outputDir,
                                    const std::string& roomName,
                                    const BakeParams& params = BakeParams());

private:
    // Slab-method ray vs unit block AABB.  Returns true with t set if hit.
    static bool RayBlock(glm::vec3 o, glm::vec3 d, const RoomBlock& b, float& t);

    // Slab-method ray vs arbitrary AABB. Returns true with t set if hit.
    static bool RayAABB(glm::vec3 o, glm::vec3 d,
                        glm::vec3 mn, glm::vec3 mx, float& t);

    // Returns 1.0 if unoccluded, 0.0 if any block occludes.
    static float ShadowRay(glm::vec3 orig, glm::vec3 target,
                            const std::vector<RoomBlock>& blocks);

    // Shadow ray against arbitrary AABB list.
    struct AABB { glm::vec3 mn, mx; };
    static float ShadowRayAABB(glm::vec3 orig, glm::vec3 target,
                                const std::vector<AABB>& boxes,
                                int skipBox = -1);

    // World-space triangle for accurate ray tracing
    struct WorldTri { glm::vec3 v0, v1, v2; int entityIdx; };

    // Möller–Trumbore ray-triangle intersection
    static bool RayTriangle(glm::vec3 o, glm::vec3 d,
                            const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                            float& t);

    // Shadow ray against world-space triangle soup (accurate)
    static float ShadowRayTris(glm::vec3 orig, glm::vec3 target,
                               const std::vector<WorldTri>& tris,
                               int skipTriIdx = -1);

    // AO ray against world-space triangle soup
    static bool AORayTris(glm::vec3 orig, glm::vec3 dir, float maxDist,
                          const std::vector<WorldTri>& tris,
                          int skipTriIdx = -1);

    // Cosine-weighted sample in hemisphere around normal.
    static glm::vec3 CosSampleHemi(glm::vec3 normal, float xi1, float xi2);

    // Lightweight LCG
    struct Rng { unsigned s = 1337;
        float next() { s = s*1664525u+1013904223u;
                       return float(s>>1)/float(0x7FFFFFFFu); } };
};

} // namespace tsu
