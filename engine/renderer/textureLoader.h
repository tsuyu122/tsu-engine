#pragma once
#include <string>

namespace tsu {

// Loads an image file and uploads it as a sRGB (GL_RGBA) texture.
// Returns the GL texture ID (> 0 on success, 0 on failure).
unsigned int LoadTexture(const std::string& path);

// Loads an image as a linear (non-gamma) texture — use for normal maps.
unsigned int LoadTextureLinear(const std::string& path);

// Packs three greyscale source textures into a single RGB ORM texture:
//   R = Ambient Occlusion (default 1.0 if path empty)
//   G = Roughness         (default roughnessDefault if path empty)
//   B = Metallic          (default metallicDefault  if path empty)
// If ALL paths are empty AND all defaults match the previous call,
// the cached texture ID from a previous identical pack is returned.
// Always deletes the old ORMID (if > 0) before creating a new one.
unsigned int PackORM(const std::string& aoPath,
                     const std::string& roughPath,
                     const std::string& metalPath,
                     float defaultAO    = 1.0f,
                     float defaultRough = 0.5f,
                     float defaultMetal = 0.0f);

} // namespace tsu
