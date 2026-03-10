#pragma once
#include "../renderer/mesh.h"
#include <glm/glm.hpp>
#include <string>

namespace tsu {

struct MeshRendererComponent
{
    Mesh*       MeshPtr  = nullptr;
    std::string MeshType;   // "cube","sphere","plane","cylinder","capsule","pyramid" or ""

    // ---- Tri-planar lightmaps (set by LightmapManager at runtime) ----
    // X projection uses world YZ, Y projection uses world XZ, Z projection uses world XY.
    unsigned int LightmapID  = 0;         // legacy/main map (kept for compatibility)
    glm::vec4    LightmapST  = {1,1,0,0}; // atlas scaleOffset (xy=scale, zw=offset)
    int          LightmapIndex = -1;
    unsigned int LightmapIDX = 0;
    unsigned int LightmapIDY = 0;
    unsigned int LightmapIDZ = 0;
    glm::vec4    LightmapSTX = {1,1,0,0};
    glm::vec4    LightmapSTY = {1,1,0,0};
    glm::vec4    LightmapSTZ = {1,1,0,0};
};

}
