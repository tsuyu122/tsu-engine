#pragma once
#include "../renderer/mesh.h"
#include <glm/glm.hpp>
#include <string>

namespace tsu {

struct MeshRendererComponent
{
    Mesh*       MeshPtr  = nullptr;
    std::string MeshType;   // "cube","sphere","plane","cylinder","capsule","pyramid" or ""

    // ---- Lightmap (set by LightmapManager at runtime) ----
    unsigned int LightmapID = 0;        // GL texture ID; 0 = no lightmap
    glm::vec4    LightmapST = {1,1,0,0}; // xy=scale, zw=offset for world-XZ UV
};

}
