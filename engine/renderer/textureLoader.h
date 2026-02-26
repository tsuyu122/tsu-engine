#pragma once
#include <string>

namespace tsu {

// Loads an image file and uploads it to a GL texture.
// Returns the GL texture ID (> 0 on success, 0 on failure).
unsigned int LoadTexture(const std::string& path);

} // namespace tsu
