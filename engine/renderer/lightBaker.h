#pragma once
#include "scene/scene.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

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
        int   resolution    = 128;   // output texture size (square)
        int   aoSamples     = 48;    // hemisphere samples for AO
        float aoRadius      = 3.5f;  // max AO occlusion distance (blocks)
        float ambientLevel  = 0.10f; // minimum ambient (0..1)
        float directScale   = 0.85f; // direct light contribution scale
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

    // Bake the room. 'lights' can be built with ExtractLights().
    static BakeResult BakeRoom(const RoomTemplate& room,
                                const std::vector<LightInfo>& lights,
                                const BakeParams& params = BakeParams());

    // Build LightInfo list from the current scene (call from UI/Application).
    static std::vector<LightInfo> ExtractLights(const Scene& scene);

    // Bake, save TGA, return the saved path ("" on failure).
    static std::string BakeAndSave(const RoomTemplate& room,
                                    const std::vector<LightInfo>& lights,
                                    const std::string& outputDir,
                                    const std::string& roomName,
                                    const BakeParams& params = BakeParams());

private:
    // Slab-method ray vs unit block AABB.  Returns true with t set if hit.
    static bool RayBlock(glm::vec3 o, glm::vec3 d, const RoomBlock& b, float& t);

    // Returns 1.0 if unoccluded, 0.0 if any block occludes.
    static float ShadowRay(glm::vec3 orig, glm::vec3 target,
                            const std::vector<RoomBlock>& blocks);

    // Cosine-weighted sample in hemisphere around normal.
    static glm::vec3 CosSampleHemi(glm::vec3 normal, float xi1, float xi2);

    // Lightweight LCG
    struct Rng { unsigned s = 1337;
        float next() { s = s*1664525u+1013904223u;
                       return float(s>>1)/float(0x7FFFFFFFu); } };
};

} // namespace tsu
