#include "renderer/lightBaker.h"
#include "renderer/lightmapManager.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tsu {

// ---- Ray helpers ----

bool LightBaker::RayBlock(glm::vec3 o, glm::vec3 d, const RoomBlock& b, float& t)
{
    glm::vec3 bmin((float)b.X - 0.5f, (float)b.Y - 0.5f, (float)b.Z - 0.5f);
    glm::vec3 bmax((float)b.X + 0.5f, (float)b.Y + 0.5f, (float)b.Z + 0.5f);
    float tmin = 1e-4f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        float inv = (std::abs(d[i]) > 1e-9f) ? 1.0f / d[i] : 1e30f;
        float t1  = (bmin[i] - o[i]) * inv;
        float t2  = (bmax[i] - o[i]) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    t = tmin;
    return true;
}

float LightBaker::ShadowRay(glm::vec3 orig, glm::vec3 target,
                              const std::vector<RoomBlock>& blocks)
{
    glm::vec3 delta = target - orig;
    float dist = glm::length(delta);
    if (dist < 1e-5f) return 1.0f;
    glm::vec3 dir = delta / dist;
    glm::vec3 o   = orig + dir * 0.07f;
    float maxT    = dist - 0.12f;
    for (const auto& blk : blocks) {
        float th;
        if (RayBlock(o, dir, blk, th) && th < maxT) return 0.0f;
    }
    return 1.0f;
}

glm::vec3 LightBaker::CosSampleHemi(glm::vec3 normal, float xi1, float xi2)
{
    float r   = sqrtf(xi1);
    float phi = 2.0f * (float)M_PI * xi2;
    float lx  = r * cosf(phi);
    float lz  = r * sinf(phi);
    float ly  = sqrtf(std::max(0.0f, 1.0f - xi1));
    glm::vec3 up = (std::abs(normal.y) < 0.999f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    glm::vec3 T  = glm::normalize(glm::cross(up, normal));
    glm::vec3 B  = glm::cross(normal, T);
    return glm::normalize(T * lx + normal * ly + B * lz);
}

// ---- Kelvin approximation (matches renderer internal function) ----
static glm::vec3 KelvinRGB(float K)
{
    float t = K / 100.0f;
    glm::vec3 c(1.0f);
    if (t <= 66.0f) {
        c.g = glm::clamp(0.39008157f * logf(t) - 0.63184144f, 0.0f, 1.0f);
    } else {
        c.r = glm::clamp(1.29293618f * powf(t - 60.0f, -0.1332047592f), 0.0f, 1.0f);
        c.g = glm::clamp(1.12989086f * powf(t - 60.0f, -0.0755148492f), 0.0f, 1.0f);
    }
    if (t >= 66.0f) c.b = 1.0f;
    else if (t <= 19.0f) c.b = 0.0f;
    else c.b = glm::clamp(0.54320678f * logf(t - 10.0f) - 1.19625408f, 0.0f, 1.0f);
    return c;
}

// ---- ExtractLights ----

std::vector<LightBaker::LightInfo> LightBaker::ExtractLights(const Scene& scene)
{
    std::vector<LightInfo> out;
    for (int i = 0; i < (int)scene.Lights.size(); ++i) {
        const auto& lc = scene.Lights[i];
        if (!lc.Active || !lc.Enabled) continue;
        LightInfo li;
        li.type       = lc.Type;
        li.color      = lc.Color * KelvinRGB(lc.Temperature) * lc.Intensity;
        li.range      = lc.Range;
        li.innerAngle = lc.InnerAngle;
        li.outerAngle = lc.OuterAngle;
        glm::mat4 wm  = scene.GetEntityWorldMatrix(i);
        li.position   = glm::vec3(wm[3]);
        li.direction  = glm::normalize(glm::vec3(wm[0])); // +X forward convention
        out.push_back(li);
    }
    return out;
}

// ---- BakeRoom ----

LightBaker::BakeResult LightBaker::BakeRoom(const RoomTemplate& room,
                                              const std::vector<LightInfo>& lights,
                                              const BakeParams& params)
{
    BakeResult result;
    if (room.Blocks.empty()) return result;

    // Bounding box of all blocks
    int minX = room.Blocks[0].X, maxX = minX;
    int minZ = room.Blocks[0].Z, maxZ = minZ;
    int maxY = room.Blocks[0].Y;
    for (const auto& b : room.Blocks) {
        minX = std::min(minX, b.X); maxX = std::max(maxX, b.X);
        minZ = std::min(minZ, b.Z); maxZ = std::max(maxZ, b.Z);
        maxY = std::max(maxY, b.Y);
    }

    float szX = (float)(maxX - minX + 1);
    float szZ = (float)(maxZ - minZ + 1);
    // ST: u = worldX * ST.x + ST.z   (v likewise with z)
    result.lightmapST = { 1.0f/szX, 1.0f/szZ,
                          -(float)minX/szX, -(float)minZ/szZ };
    int W = params.resolution, H = params.resolution;
    result.width  = W;
    result.height = H;
    result.pixels.assign(W * H * 3, 0);

    float topY = (float)maxY + 0.55f;
    Rng rng;

    for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px)
    {
        float u = ((float)px + 0.5f) / (float)W;
        float v = ((float)py + 0.5f) / (float)H;

        // Invert ST to get world position
        float worldX = (u - result.lightmapST.z) / result.lightmapST.x;
        float worldZ = (v - result.lightmapST.w) / result.lightmapST.y;
        glm::vec3 samplePos(worldX, topY, worldZ);
        glm::vec3 normal(0, 1, 0);

        // ---- Ambient Occlusion ----
        float ao = 0.0f;
        glm::vec3 aoOrig = samplePos + normal * 0.06f;
        for (int s = 0; s < params.aoSamples; ++s) {
            glm::vec3 dir = CosSampleHemi(normal, rng.next(), rng.next());
            bool hit = false;
            for (const auto& blk : room.Blocks) {
                float th;
                if (RayBlock(aoOrig, dir, blk, th) && th < params.aoRadius)
                { hit = true; break; }
            }
            if (!hit) ao += 1.0f;
        }
        ao /= (float)params.aoSamples;

        // ---- Direct Light ----
        glm::vec3 direct(0.0f);
        for (const auto& li : lights) {
            if (li.type == LightType::Directional) {
                glm::vec3 L = glm::normalize(-li.direction);
                float vis = ShadowRay(samplePos, samplePos + L * 100.0f, room.Blocks);
                direct += li.color * vis * std::max(0.0f, L.y);
            }
            else if (li.type == LightType::Point) {
                float dist = glm::distance(samplePos, li.position);
                if (dist < li.range) {
                    float vis = ShadowRay(samplePos, li.position, room.Blocks);
                    float att = 1.0f - dist / li.range; att *= att;
                    glm::vec3 L = glm::normalize(li.position - samplePos);
                    direct += li.color * vis * att * std::max(0.0f, L.y);
                }
            }
            else if (li.type == LightType::Spot) {
                float dist = glm::distance(samplePos, li.position);
                if (dist < li.range) {
                    float vis = ShadowRay(samplePos, li.position, room.Blocks);
                    float att = 1.0f - dist / li.range; att *= att;
                    glm::vec3 L = glm::normalize(li.position - samplePos);
                    float cosT  = glm::dot(-L, glm::normalize(li.direction));
                    float outer = cosf(glm::radians(li.outerAngle));
                    float inner = cosf(glm::radians(li.innerAngle));
                    float spot  = glm::clamp((cosT - outer) /
                                  std::max(inner - outer, 0.001f), 0.0f, 1.0f);
                    direct += li.color * vis * att * spot * std::max(0.0f, L.y);
                }
            }
        }

        // ---- Combine + Tonemap ----
        glm::vec3 col = glm::vec3(params.ambientLevel) * ao
                      + direct * params.directScale * ao;
        col = col / (col + glm::vec3(1.0f));  // Reinhard
        col = glm::pow(glm::clamp(col, 0.0f, 1.0f), glm::vec3(1.0f / 2.2f)); // gamma

        int idx = (py * W + px) * 3;
        result.pixels[idx+0] = (unsigned char)(col.r * 255.0f + 0.5f);
        result.pixels[idx+1] = (unsigned char)(col.g * 255.0f + 0.5f);
        result.pixels[idx+2] = (unsigned char)(col.b * 255.0f + 0.5f);
    }

    return result;
}

std::string LightBaker::BakeAndSave(const RoomTemplate& room,
                                     const std::vector<LightInfo>& lights,
                                     const std::string& outputDir,
                                     const std::string& roomName,
                                     const BakeParams& params)
{
    BakeResult res = BakeRoom(room, lights, params);
    if (res.pixels.empty()) return "";

    std::string fname = roomName;
    for (auto& c : fname) if (c == ' ' || c == '/' || c == '\\') c = '_';
    std::string path = outputDir + "/" + fname + "_lm.tga";

    if (!LightmapManager::SaveTGA(path, res.width, res.height, res.pixels.data()))
        return "";
    return path;
}

} // namespace tsu
