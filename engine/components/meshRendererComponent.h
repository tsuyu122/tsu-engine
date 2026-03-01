#pragma once
#include "../renderer/mesh.h"
#include <string>

namespace tsu {

struct MeshRendererComponent
{
    Mesh*       MeshPtr  = nullptr;
    std::string MeshType;   // "cube","sphere","plane","cylinder","capsule","pyramid" or ""
};

}