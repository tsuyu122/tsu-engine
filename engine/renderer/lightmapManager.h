#pragma once
#include <string>
#include <unordered_map>

namespace tsu {

// Manages lightmap textures with reference counting.
// Textures are loaded from disk the first time and cached; subsequent Load()
// calls with the same path simply increment the ref count.
// When ref count drops to 0 on Release(), the GL texture is freed.
class LightmapManager
{
public:
    static LightmapManager& Instance();

    // Load (or retrieve cached) lightmap from disk.
    // Returns GL texture ID (> 0 on success, 0 on failure).
    unsigned int Load(const std::string& path);

    // Create a GL_RGB16F texture directly from linear float RGB data.
    // Returns GL texture ID (> 0 on success, 0 on failure).
    // The data is NOT cached by path; caller is responsible for evicting old textures.
    static unsigned int LoadFromFloat(int w, int h, const float* rgbFloat);

    // Returns the cached GL texture ID without loading or changing the ref count.
    // Returns 0 if the path has not been loaded yet.
    unsigned int GetCachedID(const std::string& path) const;

    // Decrement ref count; deletes GL texture when it reaches 0.
    void Release(const std::string& path);

    // Force-evict a path from cache (deletes GL texture regardless of refcount).
    // Use before re-Load() when the file on disk has changed (e.g. re-bake).
    void Evict(const std::string& path);

    // Free all loaded lightmaps (call on shutdown or scene unload).
    void ReleaseAll();

    // ---- Static helpers ----

    // Write raw RGB data as an uncompressed TGA file.
    // rgb must be w*h*3 bytes, row-major, origin at top-left.
    // Returns true on success.
    static bool SaveTGA(const std::string& path, int w, int h,
                        const unsigned char* rgb);

private:
    struct Entry { unsigned int texId = 0; int refs = 0; };
    std::unordered_map<std::string, Entry> m_Cache;
};

} // namespace tsu
