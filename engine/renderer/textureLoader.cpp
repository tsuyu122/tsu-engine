#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <glad/glad.h>
#include "renderer/textureLoader.h"
#include <vector>
#include <cstdio>

namespace tsu {

unsigned int LoadTexture(const std::string& path)
{
    stbi_set_flip_vertically_on_load(true); // OpenGL convention: V=0 at bottom
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4); // force RGBA
    if (!data) return 0;

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    return id;
}

unsigned int LoadTextureLinear(const std::string& path)
{
    if (path.empty()) return 0;
    stbi_set_flip_vertically_on_load(false); // normal maps: keep as-is
    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    stbi_set_flip_vertically_on_load(true); // restore global default
    if (!data) return 0;

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return id;
}

// ---------------------------------------------------------------------------
// PackORM — packs AO(R), Roughness(G), Metallic(B) into one 3-channel texture
// ---------------------------------------------------------------------------
unsigned int PackORM(const std::string& aoPath,
                     const std::string& roughPath,
                     const std::string& metalPath,
                     float defaultAO,
                     float defaultRough,
                     float defaultMetal)
{
    // Load each source as grey (1 channel).  Empty path  uses a flat value.
    auto loadGrey = [](const std::string& p, float def, int& outW, int& outH)
        -> std::vector<unsigned char>
    {
        if (!p.empty())
        {
            stbi_set_flip_vertically_on_load(true);
            int w, h, ch;
            unsigned char* raw = stbi_load(p.c_str(), &w, &h, &ch, 1);
            if (raw)
            {
                outW = w; outH = h;
                std::vector<unsigned char> v(raw, raw + w * h);
                stbi_image_free(raw);
                return v;
            }
            printf("[PackORM] WARN: failed to load '%s': %s\n",
                   p.c_str(), stbi_failure_reason());
        }
        outW = 0; outH = 0;
        return {};
    };

    int wAO=0, hAO=0, wRgh=0, hRgh=0, wMet=0, hMet=0;
    auto ao  = loadGrey(aoPath,    defaultAO,    wAO,  hAO);
    auto rgh = loadGrey(roughPath, defaultRough, wRgh, hRgh);
    auto met = loadGrey(metalPath, defaultMetal, wMet, hMet);

    // Determine output resolution: use first valid size or fall back to 1x1
    int W = 1, H = 1;
    if (wAO  > 0) { W = wAO;  H = hAO; }
    else if (wRgh > 0) { W = wRgh; H = hRgh; }
    else if (wMet > 0) { W = wMet; H = hMet; }

    const unsigned char cAO    = (unsigned char)(defaultAO    * 255.0f);
    const unsigned char cRough = (unsigned char)(defaultRough * 255.0f);
    const unsigned char cMetal = (unsigned char)(defaultMetal * 255.0f);

    auto sample = [&](const std::vector<unsigned char>& src,
                      int srcW, int srcH,
                      unsigned char def, int px, int py) -> unsigned char
    {
        if (src.empty() || srcW == 0) return def;
        // Nearest-neighbour resample if dimensions differ
        int sx = (srcW == W) ? px : (int)((float)px / W * srcW);
        int sy = (srcH == H) ? py : (int)((float)py / H * srcH);
        sx = sx < 0 ? 0 : sx >= srcW ? srcW-1 : sx;
        sy = sy < 0 ? 0 : sy >= srcH ? srcH-1 : sy;
        return src[sy * srcW + sx];
    };

    std::vector<unsigned char> packed;
    packed.reserve(W * H * 3);
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px)
        {
            packed.push_back(sample(ao,  wAO,  hAO,  cAO,    px, py));  // R = AO
            packed.push_back(sample(rgh, wRgh, hRgh, cRough, px, py));  // G = Roughness
            packed.push_back(sample(met, wMet, hMet, cMetal, px, py));  // B = Metallic
        }

    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, packed.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

} // namespace tsu
