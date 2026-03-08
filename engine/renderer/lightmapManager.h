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

    // Returns the cached GL texture ID without loading or changing the ref count.
    // Returns 0 if the path has not been loaded yet.
    unsigned int GetCachedID(const std::string& path) const;

    // Decrement ref count; deletes GL texture when it reaches 0.
    void Release(const std::string& path);

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
