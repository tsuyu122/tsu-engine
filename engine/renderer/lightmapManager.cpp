// stb_image.h is implemented in textureLoader.cpp; include header only here
#include "renderer/lightmapManager.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <cstdio>
#include <cstring>

namespace tsu {

LightmapManager& LightmapManager::Instance()
{
    static LightmapManager s;
    return s;
}

unsigned int LightmapManager::Load(const std::string& path)
{
    auto it = m_Cache.find(path);
    if (it != m_Cache.end())
    {
        it->second.refs++;
        return it->second.texId;
    }

    // Load via stb_image (already defined in textureLoader.cpp)
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 3);
    if (!data) return 0;

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);

    m_Cache[path] = {id, 1};
    return id;
}

unsigned int LightmapManager::GetCachedID(const std::string& path) const
{
    auto it = m_Cache.find(path);
    return (it != m_Cache.end()) ? it->second.texId : 0u;
}

void LightmapManager::Release(const std::string& path)
{
    auto it = m_Cache.find(path);
    if (it == m_Cache.end()) return;
    if (--it->second.refs <= 0)
    {
        glDeleteTextures(1, &it->second.texId);
        m_Cache.erase(it);
    }
}

void LightmapManager::ReleaseAll()
{
    for (auto& [path, e] : m_Cache)
        glDeleteTextures(1, &e.texId);
    m_Cache.clear();
}

// ---- TGA writer ----
// TGA header: 18 bytes, then raw BGR data (we write RGB, compatible with most readers)
bool LightmapManager::SaveTGA(const std::string& path, int w, int h,
                               const unsigned char* rgb)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;

    // Minimal uncompressed TGA header
    unsigned char hdr[18] = {};
    hdr[2]  = 2;               // image type: uncompressed true-colour
    hdr[12] = (unsigned char)(w & 0xFF);
    hdr[13] = (unsigned char)((w >> 8) & 0xFF);
    hdr[14] = (unsigned char)(h & 0xFF);
    hdr[15] = (unsigned char)((h >> 8) & 0xFF);
    hdr[16] = 24;              // bits per pixel
    hdr[17] = 0x20;            // origin: top-left

    fwrite(hdr, 1, 18, f);

    // TGA expects BGR; flip R and B channels
    for (int i = 0; i < w * h; ++i)
    {
        unsigned char b = rgb[i*3+2];
        unsigned char g = rgb[i*3+1];
        unsigned char r = rgb[i*3+0];
        fputc(b, f); fputc(g, f); fputc(r, f);
    }
    fclose(f);
    return true;
}

} // namespace tsu
