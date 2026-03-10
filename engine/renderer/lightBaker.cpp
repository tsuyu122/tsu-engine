#include "renderer/lightBaker.h"
#include "renderer/lightmapManager.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <limits>

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
    glm::vec3 o   = orig + dir * 0.01f;
    float maxT    = dist - 0.02f;
    for (const auto& blk : blocks) {
        float th;
        if (RayBlock(o, dir, blk, th) && th < maxT) return 0.0f;
    }
    return 1.0f;
}

bool LightBaker::RayAABB(glm::vec3 o, glm::vec3 d, glm::vec3 mn, glm::vec3 mx, float& t)
{
    float tmin = 1e-4f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        float inv = (std::abs(d[i]) > 1e-9f) ? 1.0f / d[i] : 1e30f;
        float t1  = (mn[i] - o[i]) * inv;
        float t2  = (mx[i] - o[i]) * inv;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    t = tmin;
    return true;
}

float LightBaker::ShadowRayAABB(glm::vec3 orig, glm::vec3 target,
                                  const std::vector<AABB>& boxes,
                                  int skipBox)
{
    glm::vec3 delta = target - orig;
    float dist = glm::length(delta);
    if (dist < 1e-5f) return 1.0f;
    glm::vec3 dir = delta / dist;
    glm::vec3 o   = orig + dir * 0.01f;
    float maxT    = dist - 0.02f;
    for (size_t i = 0; i < boxes.size(); ++i) {
        if ((int)i == skipBox) continue;
        const auto& box = boxes[i];
        float th;
        if (RayAABB(o, dir, box.mn, box.mx, th) && th < maxT) return 0.0f;
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

// ---- Möller–Trumbore ray-triangle intersection ----
bool LightBaker::RayTriangle(glm::vec3 o, glm::vec3 d,
                              const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                              float& t)
{
    const float EPS = 1e-7f;
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    glm::vec3 h  = glm::cross(d, e2);
    float a = glm::dot(e1, h);
    if (a > -EPS && a < EPS) return false;
    float f = 1.0f / a;
    glm::vec3 s = o - v0;
    float u = f * glm::dot(s, h);
    if (u < 0.0f || u > 1.0f) return false;
    glm::vec3 q = glm::cross(s, e1);
    float v = f * glm::dot(d, q);
    if (v < 0.0f || u + v > 1.0f) return false;
    t = f * glm::dot(e2, q);
    return t > EPS;
}

float LightBaker::ShadowRayTris(glm::vec3 orig, glm::vec3 target,
                                  const std::vector<WorldTri>& tris,
                                  int skipTriIdx)
{
    glm::vec3 delta = target - orig;
    float dist = glm::length(delta);
    if (dist < 1e-5f) return 1.0f;
    glm::vec3 dir = delta / dist;
    const float minT = 0.02f;
    float maxT    = dist - 0.02f;
    for (int i = 0; i < (int)tris.size(); ++i) {
        if (i == skipTriIdx) continue;
        float th;
        if (RayTriangle(orig, dir, tris[i].v0, tris[i].v1, tris[i].v2, th) && th > minT && th < maxT)
            return 0.0f;
    }
    return 1.0f;
}

bool LightBaker::AORayTris(glm::vec3 orig, glm::vec3 dir, float maxDist,
                             const std::vector<WorldTri>& tris,
                             int skipTriIdx)
{
    const float minT = 0.02f;
    for (int i = 0; i < (int)tris.size(); ++i) {
        if (i == skipTriIdx) continue;
        float th;
        if (RayTriangle(orig, dir, tris[i].v0, tris[i].v1, tris[i].v2, th) && th > minT && th < maxDist)
            return true;
    }
    return false;
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
        li.direction  = glm::normalize(glm::vec3(wm[0])); // +X forward (matches runtime renderer)
        out.push_back(li);
    }
    return out;
}

// ---- ExtractRoomLights ----

std::vector<LightBaker::LightInfo> LightBaker::ExtractRoomLights(const RoomTemplate& room)
{
    std::vector<LightInfo> out;
    for (const auto& node : room.Interior) {
        if (!node.HasLight) continue;
        const auto& lc = node.Light;
        if (!lc.Enabled) continue;
        LightInfo li;
        li.type       = lc.Type;
        li.color      = lc.Color * KelvinRGB(lc.Temperature) * lc.Intensity;
        li.range      = lc.Range;
        li.innerAngle = lc.InnerAngle;
        li.outerAngle = lc.OuterAngle;
        li.position   = node.Transform.Position;
        // Build direction from rotation (forward = +X in engine convention)
        glm::mat4 rotMat = glm::mat4(1.0f);
        rotMat = glm::rotate(rotMat, glm::radians(node.Transform.Rotation.y), glm::vec3(0,1,0));
        rotMat = glm::rotate(rotMat, glm::radians(node.Transform.Rotation.x), glm::vec3(1,0,0));
        rotMat = glm::rotate(rotMat, glm::radians(node.Transform.Rotation.z), glm::vec3(0,0,1));
        li.direction = glm::normalize(glm::vec3(rotMat[0]));
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

    // ---- Pass 1: compute HDR linear values ----
    std::vector<glm::vec3> hdr(W * H);
    float hdrMax = 0.001f;

    for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px)
    {
        float u = ((float)px + 0.5f) / (float)W;
        float v = ((float)py + 0.5f) / (float)H;

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

        glm::vec3 col = glm::vec3(params.ambientLevel) * ao
                      + direct * params.directScale * ao;
        hdr[py * W + px] = col;
        hdrMax = std::max(hdrMax, std::max(col.r, std::max(col.g, col.b)));
    }

    // ---- Pass 2: normalize by max → [0,1], gamma-encode, write bytes ----
    float invMax = 1.0f / hdrMax;
    for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px)
    {
        glm::vec3 col = hdr[py * W + px] * invMax;
        col = glm::pow(glm::clamp(col, 0.0f, 1.0f), glm::vec3(1.0f / 2.2f));
        int idx = (py * W + px) * 3;
        result.pixels[idx+0] = (unsigned char)(col.r * 255.0f + 0.5f);
        result.pixels[idx+1] = (unsigned char)(col.g * 255.0f + 0.5f);
        result.pixels[idx+2] = (unsigned char)(col.b * 255.0f + 0.5f);
    }

    return result;
}

// ---- BakeScene ----

LightBaker::BakeResult LightBaker::BakeScene(const Scene& scene,
                                               const std::vector<LightInfo>& lights,
                                               const BakeParams& params)
{
    BakeResult result;

    // Build AABB occluders from every entity that has a mesh
    std::vector<AABB> occ;
    float gMinX = 1e30f, gMaxX = -1e30f;
    float gMinZ = 1e30f, gMaxZ = -1e30f;
    float gMinY =  1e30f, gMaxY = -1e30f;

    for (int i = 0; i < (int)scene.MeshRenderers.size(); ++i)
    {
        if (scene.MeshRenderers[i].MeshPtr == nullptr) continue;
        glm::mat4 wm = scene.GetEntityWorldMatrix(i);
        glm::vec3 pos(wm[3]);
        float hx = glm::length(glm::vec3(wm[0])) * 0.5f;
        float hy = glm::length(glm::vec3(wm[1])) * 0.5f;
        float hz = glm::length(glm::vec3(wm[2])) * 0.5f;

        AABB box;
        box.mn = pos - glm::vec3(hx, hy, hz);
        box.mx = pos + glm::vec3(hx, hy, hz);
        occ.push_back(box);

        gMinX = std::min(gMinX, box.mn.x); gMaxX = std::max(gMaxX, box.mx.x);
        gMinZ = std::min(gMinZ, box.mn.z); gMaxZ = std::max(gMaxZ, box.mx.z);
        gMinY = std::min(gMinY, box.mn.y); gMaxY = std::max(gMaxY, box.mx.y);
    }

    if (occ.empty()) return result;

    const float pad = 1.0f;
    gMinX -= pad; gMinZ -= pad;
    gMaxX += pad; gMaxZ += pad;

    float szX = gMaxX - gMinX;
    float szZ = gMaxZ - gMinZ;
    if (szX < 0.01f || szZ < 0.01f) return result;

    // ST maps world XZ → [0,1] UV:  u = worldX * ST.x + ST.z
    result.lightmapST = { 1.0f/szX, 1.0f/szZ, -gMinX/szX, -gMinZ/szZ };

    int W = params.resolution, H = params.resolution;
    result.width  = W;
    result.height = H;
    result.pixels.assign(W * H * 3, 0);

    // Near-bottom fallback for empty XZ cells.
    // Using scene midpoint made the bake almost uniform in many scenes.
    float baseY = gMinY + 0.05f;
    Rng rng;
    glm::vec3 upN(0, 1, 0);

    // ---- Pass 1: compute HDR linear values ----
    std::vector<glm::vec3> hdr(W * H);
    float hdrMax = 0.001f;

    for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px)
    {
        float u = ((float)px + 0.5f) / (float)W;
        float v = ((float)py + 0.5f) / (float)H;

        float worldX = (u - result.lightmapST.z) / result.lightmapST.x;
        float worldZ = (v - result.lightmapST.w) / result.lightmapST.y;

        // --- Adaptive sample height ---
        // Pick the highest surface under this XZ so lighting changes across objects/floor.
        float sampleY = baseY;
        bool hasSurface = false;
        for (const auto& box : occ) {
            if (worldX >= box.mn.x && worldX <= box.mx.x &&
                worldZ >= box.mn.z && worldZ <= box.mx.z)
            {
                hasSurface = true;
                sampleY = std::max(sampleY, box.mx.y);
            }
        }
        sampleY += hasSurface ? 0.06f : 0.0f;
        glm::vec3 samplePos(worldX, sampleY, worldZ);

        // ---- Ambient Occlusion (upper hemisphere) ----
        float ao = 0.0f;
        glm::vec3 aoOrig = samplePos + upN * 0.06f;
        for (int s = 0; s < params.aoSamples; ++s) {
            glm::vec3 dir = CosSampleHemi(upN, rng.next(), rng.next());
            bool hit = false;
            for (const auto& box : occ) {
                float th;
                if (RayAABB(aoOrig, dir, box.mn, box.mx, th) && th < params.aoRadius)
                { hit = true; break; }
            }
            if (!hit) ao += 1.0f;
        }
        ao /= (float)params.aoSamples;

        // ---- Direct Light ----
        // Slight NdotL floor keeps interior fixtures contributing while
        // preserving directional shading contrast.
        glm::vec3 direct(0.0f);
        for (const auto& li : lights) {
            if (li.type == LightType::Directional) {
                glm::vec3 L   = glm::normalize(-li.direction);
                float vis     = ShadowRayAABB(samplePos, samplePos + L * 100.0f, occ);
                float ndotl   = std::max(0.10f, glm::dot(upN, L));
                direct += li.color * vis * ndotl;
            }
            else if (li.type == LightType::Point) {
                float dist = glm::distance(samplePos, li.position);
                if (dist < li.range) {
                    float vis = ShadowRayAABB(samplePos, li.position, occ);
                    float att = 1.0f - dist / li.range; att *= att;
                    glm::vec3 L = glm::normalize(li.position - samplePos);
                    float ndotl = std::max(0.10f, glm::dot(upN, L));
                    direct += li.color * vis * att * ndotl;
                }
            }
            else if (li.type == LightType::Spot) {
                float dist = glm::distance(samplePos, li.position);
                if (dist < li.range) {
                    float vis = ShadowRayAABB(samplePos, li.position, occ);
                    float att = 1.0f - dist / li.range; att *= att;
                    glm::vec3 L = glm::normalize(li.position - samplePos);
                    float cosT  = glm::dot(-L, glm::normalize(li.direction));
                    float outer = cosf(glm::radians(li.outerAngle));
                    float inner = cosf(glm::radians(li.innerAngle));
                    float spot  = glm::clamp((cosT - outer) /
                                  std::max(inner - outer, 0.001f), 0.0f, 1.0f);
                    float ndotl = std::max(0.10f, glm::dot(upN, L));
                    direct += li.color * vis * att * spot * ndotl;
                }
            }
        }

        glm::vec3 col = glm::vec3(params.ambientLevel) * ao
                      + direct * params.directScale;
        hdr[py * W + px] = col;
        hdrMax = std::max(hdrMax, std::max(col.r, std::max(col.g, col.b)));
    }

    // ---- Pass 2: normalize by max with black-point stretch for contrast ----
    float invMax = 1.0f / hdrMax;
    float blackPoint = params.ambientLevel * 0.35f;
    for (int py = 0; py < H; ++py)
    for (int px = 0; px < W; ++px)
    {
        glm::vec3 col = hdr[py * W + px] * invMax;
        col = (col - glm::vec3(blackPoint)) / std::max(1.0f - blackPoint, 0.001f);
        col = glm::pow(glm::clamp(col, 0.0f, 1.0f), glm::vec3(1.0f / 2.2f));
        int idx = (py * W + px) * 3;
        result.pixels[idx+0] = (unsigned char)(col.r * 255.0f + 0.5f);
        result.pixels[idx+1] = (unsigned char)(col.g * 255.0f + 0.5f);
        result.pixels[idx+2] = (unsigned char)(col.b * 255.0f + 0.5f);
    }

    return result;
}

LightBaker::SceneBakeResult LightBaker::BakeSceneTriPlanar(const Scene& scene,
                                                            const std::vector<LightInfo>& lights,
                                                            const BakeParams& params)
{
    SceneBakeResult out;

    std::vector<AABB> occ;
    for (int i = 0; i < (int)scene.MeshRenderers.size(); ++i)
    {
        if (scene.MeshRenderers[i].MeshPtr == nullptr) continue;
        bool isStatic = (i < (int)scene.EntityStatic.size()) ? scene.EntityStatic[i] : true;
        bool receiveLM = (i < (int)scene.EntityReceiveLightmap.size()) ? scene.EntityReceiveLightmap[i] : true;
        if (!isStatic || !receiveLM) continue;
        glm::mat4 wm = scene.GetEntityWorldMatrix(i);
        glm::vec3 pos(wm[3]);
        float hx = glm::length(glm::vec3(wm[0])) * 0.5f;
        float hy = glm::length(glm::vec3(wm[1])) * 0.5f;
        float hz = glm::length(glm::vec3(wm[2])) * 0.5f;

        AABB box;
        box.mn = pos - glm::vec3(hx, hy, hz);
        box.mx = pos + glm::vec3(hx, hy, hz);
        occ.push_back(box);
    }
    if (occ.empty()) return out;

    const int W = params.resolution;
    const int H = params.resolution;
    out.width  = W;
    out.height = H;

    auto bakeProj = [&](int a, int b, int c, glm::vec3 axisN,
                        std::vector<unsigned char>& pixels, glm::vec4& st)
    {
        float mnA = 1e30f, mxA = -1e30f;
        float mnB = 1e30f, mxB = -1e30f;
        float mnC = 1e30f;

        for (const auto& box : occ) {
            mnA = std::min(mnA, box.mn[a]); mxA = std::max(mxA, box.mx[a]);
            mnB = std::min(mnB, box.mn[b]); mxB = std::max(mxB, box.mx[b]);
            mnC = std::min(mnC, box.mn[c]);
        }

        const float pad = 1.0f;
        mnA -= pad; mnB -= pad;
        mxA += pad; mxB += pad;
        float szA = mxA - mnA;
        float szB = mxB - mnB;
        if (szA < 0.01f || szB < 0.01f) return;

        st = glm::vec4(1.0f / szA, 1.0f / szB, -mnA / szA, -mnB / szB);
        pixels.assign(W * H * 3, 0);

        std::vector<glm::vec3> hdr(W * H, glm::vec3(0.0f));
        float hdrMax = 0.001f;
        Rng rng;

        auto skyAt = [&](const glm::vec3& dir) -> glm::vec3 {
            float t = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
            return glm::mix(scene.Sky.GroundColor, scene.Sky.ZenithColor, t);
        };

        for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px)
        {
            float u = ((float)px + 0.5f) / (float)W;
            float v = ((float)py + 0.5f) / (float)H;

            float wA = (u - st.z) / st.x;
            float wB = (v - st.w) / st.y;
            float wC = mnC + 0.05f;
            bool hasSurface = false;

            for (const auto& box : occ) {
                if (wA >= box.mn[a] && wA <= box.mx[a] &&
                    wB >= box.mn[b] && wB <= box.mx[b])
                {
                    hasSurface = true;
                    wC = std::max(wC, box.mx[c]);
                }
            }
            if (hasSurface) wC += 0.06f;

            glm::vec3 samplePos(0.0f);
            samplePos[a] = wA;
            samplePos[b] = wB;
            samplePos[c] = wC;

            float ao = 0.0f;
            glm::vec3 aoOrig = samplePos + axisN * 0.06f;
            for (int s = 0; s < params.aoSamples; ++s) {
                glm::vec3 dir = CosSampleHemi(axisN, rng.next(), rng.next());
                bool hit = false;
                for (const auto& box : occ) {
                    float th;
                    if (RayAABB(aoOrig, dir, box.mn, box.mx, th) && th < params.aoRadius)
                    { hit = true; break; }
                }
                if (!hit) ao += 1.0f;
            }
            ao /= (float)params.aoSamples;

            glm::vec3 direct(0.0f);
            for (const auto& li : lights) {
                if (li.type == LightType::Directional) {
                    glm::vec3 L = glm::normalize(-li.direction);
                    float vis   = ShadowRayAABB(samplePos, samplePos + L * 100.0f, occ);
                    float ndotl = std::max(0.10f, std::abs(glm::dot(axisN, L)));
                    direct += li.color * vis * ndotl;
                }
                else if (li.type == LightType::Point) {
                    float dist = glm::distance(samplePos, li.position);
                    if (dist < li.range) {
                        float vis = ShadowRayAABB(samplePos, li.position, occ);
                        float att = 1.0f - dist / li.range; att *= att;
                        glm::vec3 L = glm::normalize(li.position - samplePos);
                        float ndotl = std::max(0.10f, std::abs(glm::dot(axisN, L)));
                        direct += li.color * vis * att * ndotl;
                    }
                }
                else if (li.type == LightType::Spot) {
                    float dist = glm::distance(samplePos, li.position);
                    if (dist < li.range) {
                        float vis = ShadowRayAABB(samplePos, li.position, occ);
                        float att = 1.0f - dist / li.range; att *= att;
                        glm::vec3 L = glm::normalize(li.position - samplePos);
                        float cosT  = glm::dot(-L, glm::normalize(li.direction));
                        float outer = cosf(glm::radians(li.outerAngle));
                        float inner = cosf(glm::radians(li.innerAngle));
                        float spot  = glm::clamp((cosT - outer) /
                                      std::max(inner - outer, 0.001f), 0.0f, 1.0f);
                        float ndotl = std::max(0.10f, std::abs(glm::dot(axisN, L)));
                        direct += li.color * vis * att * spot * ndotl;
                    }
                }
            }

            // ---- Simplified indirect GI (hemisphere sampling + bounce decay) ----
            glm::vec3 gi(0.0f);
            glm::vec3 throughput(1.0f);
            glm::vec3 bounceOrigin = samplePos + axisN * 0.05f;
            const float giRayMax = std::max(params.aoRadius * 2.0f, 2.0f);
            int indirectCount = std::max(params.indirectSamples, 1);
            int bounceCount = std::max(params.bounceCount, 1);

            for (int bnc = 0; bnc < bounceCount; ++bnc)
            {
                glm::vec3 acc(0.0f);
                for (int s = 0; s < indirectCount; ++s)
                {
                    glm::vec3 dir = CosSampleHemi(axisN, rng.next(), rng.next());
                    float nearestT = giRayMax;
                    bool hit = false;
                    for (const auto& box : occ)
                    {
                        float th;
                        if (RayAABB(bounceOrigin, dir, box.mn, box.mx, th) && th < nearestT)
                        {
                            nearestT = th;
                            hit = true;
                        }
                    }

                    if (!hit)
                    {
                        acc += skyAt(dir);
                    }
                    else
                    {
                        // Very cheap bounce approximation from occluded rays.
                        // Reflection term keeps color bleed directionally plausible.
                        glm::vec3 rdir = glm::reflect(dir, axisN);
                        acc += skyAt(rdir) * 0.35f;
                    }
                }
                gi += (acc / (float)indirectCount) * throughput;
                throughput *= 0.45f;
                bounceOrigin += axisN * 0.01f;
            }

            glm::vec3 ambientSky = skyAt(axisN) * params.ambientLevel;
            glm::vec3 col = ambientSky * ao + direct * params.directScale + gi * (params.ambientLevel * 0.9f) * ao;
            hdr[py * W + px] = col;
            hdrMax = std::max(hdrMax, std::max(col.r, std::max(col.g, col.b)));
        }

        // ---- Optional denoise: simple box blur in HDR space ----
        if (params.denoiseRadius > 0)
        {
            const int r = params.denoiseRadius;
            std::vector<glm::vec3> tmp = hdr;
            for (int py = 0; py < H; ++py)
            for (int px = 0; px < W; ++px)
            {
                glm::vec3 sum(0.0f);
                int count = 0;
                for (int oy = -r; oy <= r; ++oy)
                for (int ox = -r; ox <= r; ++ox)
                {
                    int sx = std::clamp(px + ox, 0, W - 1);
                    int sy = std::clamp(py + oy, 0, H - 1);
                    sum += tmp[sy * W + sx];
                    count++;
                }
                hdr[py * W + px] = sum / (float)std::max(count, 1);
            }

            // Recompute max after filtering
            hdrMax = 0.001f;
            for (const auto& c3 : hdr)
                hdrMax = std::max(hdrMax, std::max(c3.r, std::max(c3.g, c3.b)));
        }

        float invMax = 1.0f / hdrMax;
        float blackPoint = params.ambientLevel * 0.35f;
        for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px)
        {
            glm::vec3 col = hdr[py * W + px] * invMax;
            col = (col - glm::vec3(blackPoint)) / std::max(1.0f - blackPoint, 0.001f);
            col = glm::pow(glm::clamp(col, 0.0f, 1.0f), glm::vec3(1.0f / 2.2f));
            int idx = (py * W + px) * 3;
            pixels[idx+0] = (unsigned char)(col.r * 255.0f + 0.5f);
            pixels[idx+1] = (unsigned char)(col.g * 255.0f + 0.5f);
            pixels[idx+2] = (unsigned char)(col.b * 255.0f + 0.5f);
        }
    };

    bakeProj(1, 2, 0, glm::vec3(1,0,0), out.pixelsX, out.stX); // X faces: YZ projection
    bakeProj(0, 2, 1, glm::vec3(0,1,0), out.pixelsY, out.stY); // Y faces: XZ projection
    bakeProj(0, 1, 2, glm::vec3(0,0,1), out.pixelsZ, out.stZ); // Z faces: XY projection

    return out;
}

LightBaker::AtlasBakeResult LightBaker::BakeSceneAtlasUV2(
    Scene& scene,
    const std::vector<LightInfo>& lights,
    const BakeParams& params,
    const std::function<void(float, const std::string&)>& progress)
{
    AtlasBakeResult out;
    int W = std::max(32, params.resolution);
    int H = std::max(32, params.resolution);

    auto emit = [&](float p, const char* s) {
        if (progress) progress(glm::clamp(p, 0.0f, 1.0f), s ? std::string(s) : std::string());
    };

    struct BakeEntity {
        int entity = -1;
        Mesh* mesh = nullptr;
        glm::mat4 world = glm::mat4(1.0f);
        glm::mat3 normalM = glm::mat3(1.0f);
        int triCount = 0;
        int firstWorldTri = 0; // index into worldTris for this entity's first triangle
        int x = 0, y = 0, w = 0, h = 0;
        glm::vec4 so = {1,1,0,0};
    };

    std::vector<BakeEntity> ents;
    std::vector<WorldTri> worldTris;

    emit(0.02f, "Collecting meshes");
    for (int i = 0; i < (int)scene.MeshRenderers.size(); ++i)
    {
        auto& mr = scene.MeshRenderers[i];
        if (mr.MeshPtr == nullptr) continue;
        bool isStatic = (i < (int)scene.EntityStatic.size()) ? scene.EntityStatic[i] : true;
        bool receiveLM = (i < (int)scene.EntityReceiveLightmap.size()) ? scene.EntityReceiveLightmap[i] : true;
        if (!isStatic || !receiveLM) continue;

        const auto& cv = mr.MeshPtr->GetCpuVertices();
        if (cv.size() < 3 || !mr.MeshPtr->HasValidUV2()) continue;

        glm::mat4 wm = scene.GetEntityWorldMatrix(i);
        int entIdx = (int)ents.size();
        int firstTri = (int)worldTris.size();

        // Build world-space triangles for this mesh (used for accurate ray tracing)
        for (size_t ti = 0; ti + 2 < cv.size(); ti += 3)
        {
            WorldTri wt;
            wt.v0 = glm::vec3(wm * glm::vec4(cv[ti + 0].pos, 1.0f));
            wt.v1 = glm::vec3(wm * glm::vec4(cv[ti + 1].pos, 1.0f));
            wt.v2 = glm::vec3(wm * glm::vec4(cv[ti + 2].pos, 1.0f));
            wt.entityIdx = entIdx;
            worldTris.push_back(wt);
        }

        BakeEntity be;
        be.entity = i;
        be.mesh = mr.MeshPtr;
        be.world = wm;
        be.normalM = glm::mat3(glm::transpose(glm::inverse(wm)));
        be.triCount = (int)cv.size() / 3;
        be.firstWorldTri = firstTri;
        ents.push_back(be);
    }

    if (ents.empty()) {
        emit(1.0f, "ERROR: no bakeable entities");
        return out;
    }

    // Precompute geometric face normals for all world-space triangles.
    // Used during GI bounces to determine surface orientation at hit points.
    std::vector<glm::vec3> triNormals(worldTris.size());
    for (size_t i = 0; i < worldTris.size(); ++i) {
        glm::vec3 e1 = worldTris[i].v1 - worldTris[i].v0;
        glm::vec3 e2 = worldTris[i].v2 - worldTris[i].v0;
        glm::vec3 fn = glm::cross(e1, e2);
        float len = glm::length(fn);
        triNormals[i] = (len > 1e-8f) ? fn / len : glm::vec3(0, 1, 0);
    }

    // --- Diagnostic: report what was collected ---
    {
        std::string diag = "Atlas: " + std::to_string(ents.size()) + " entities, "
                         + std::to_string(worldTris.size()) + " tris, "
                         + std::to_string(lights.size()) + " lights";
        for (size_t li = 0; li < lights.size(); ++li) {
            const auto& l = lights[li];
            const char* lt = (l.type == LightType::Directional) ? "Dir" :
                             (l.type == LightType::Point) ? "Pt" :
                             (l.type == LightType::Spot) ? "Spot" : "Area";
            float lum = l.color.r + l.color.g + l.color.b;
            diag += " [" + std::string(lt) + " lum=" + std::to_string(lum) + "]";
        }
        emit(0.05f, diag.c_str());
    }

    // Shelf-pack each mesh in atlas using adaptive density for complex scenes.
    emit(0.08f, "Packing atlas charts");
    const int entCount = (int)ents.size();
    const int pad = (entCount > 24) ? 2 : 4;
    const int minSide = (entCount <= 4) ? 96 : (entCount <= 12) ? 72 : (entCount <= 24) ? 56 : 40;
    const float triScale = (entCount <= 8) ? 24.0f : (entCount <= 20) ? 18.0f : 14.0f;

    auto attemptPack = [&](float sideScale, bool allowSkip) -> bool {
        out.assignments.clear();
        int cx = pad, cy = pad, rowH = 0;
        int skipped = 0;

        for (auto& e : ents)
        {
            int base = std::max(minSide, (int)(std::sqrt((float)std::max(e.triCount, 1)) * triScale));
            int side = std::max(16, (int)(base * sideScale));
            side = std::min(side, W - 2 * pad);
            int rw = side;
            int rh = side;

            if (cx + rw + pad > W) {
                cx = pad;
                cy += rowH + pad;
                rowH = 0;
            }
            if (cy + rh + pad > H) {
                if (!allowSkip) return false;
                e.w = 0;
                e.h = 0;
                skipped++;
                continue;
            }

            e.x = cx;
            e.y = cy;
            e.w = rw;
            e.h = rh;

            float sx = (float)rw / (float)W;
            float sy = (float)rh / (float)H;
            float ox = (float)e.x / (float)W;
            float oy = (float)e.y / (float)H;
            e.so = glm::vec4(sx, sy, ox, oy);

            cx += rw + pad;
            rowH = std::max(rowH, rh);

            AtlasAssignment a;
            a.entity = e.entity;
            a.lightmapIndex = 0;
            a.scaleOffset = e.so;
            out.assignments.push_back(a);
        }
        return skipped == 0;
    };

    bool packed = false;
    const float attempts[] = {1.00f, 0.85f, 0.72f, 0.60f, 0.50f, 0.42f, 0.35f, 0.28f, 0.22f};
    for (int grow = 0; grow < 3 && !packed; ++grow) {
        for (float s : attempts) {
            if (attemptPack(s, false)) { packed = true; break; }
        }
        if (!packed) {
            if (W >= 4096 || H >= 4096) break;
            W = std::min(W * 2, 4096);
            H = std::min(H * 2, 4096);
        }
    }

    if (!packed) {
        // Last resort for extreme scenes: keep what fits so bake still succeeds.
        attemptPack(0.22f, true);
    }

    out.width = W;
    out.height = H;

    std::vector<glm::vec3> hdr(W * H, glm::vec3(0.0f));
    std::vector<int> samples(W * H, 0);

    auto bary2 = [](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, glm::vec3& bc) -> bool {
        glm::vec2 v0 = b - a, v1 = c - a, v2 = p - a;
        float d00 = glm::dot(v0, v0);
        float d01 = glm::dot(v0, v1);
        float d11 = glm::dot(v1, v1);
        float d20 = glm::dot(v2, v0);
        float d21 = glm::dot(v2, v1);
        float denom = d00 * d11 - d01 * d01;
        if (std::abs(denom) < 1e-12f) return false;
        float v = (d11 * d20 - d01 * d21) / denom;
        float w = (d00 * d21 - d01 * d20) / denom;
        float u = 1.0f - v - w;
        bc = glm::vec3(u, v, w);
        return u >= -1e-4f && v >= -1e-4f && w >= -1e-4f;
    };

    auto skyAt = [&](const glm::vec3& dir) -> glm::vec3 {
        float t = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
        return glm::mix(scene.Sky.GroundColor, scene.Sky.ZenithColor, t);
    };

    // Find the closest triangle hit along a ray (returns t and triIdx, -1 if no hit).
    struct RayHit { float t; int triIdx; };
    auto raycastClosest = [&](glm::vec3 orig, glm::vec3 dir, float maxDist, int skipTri) -> RayHit {
        RayHit best = { maxDist, -1 };
        const float minT = 0.015f;
        for (int i = 0; i < (int)worldTris.size(); ++i) {
            if (i == skipTri) continue;
            float t;
            if (RayTriangle(orig, dir, worldTris[i].v0, worldTris[i].v1, worldTris[i].v2, t))
                if (t > minT && t < best.t) { best.t = t; best.triIdx = i; }
        }
        return best;
    };

    // Compute direct lighting at an arbitrary point with a given normal.
    // Used both for primary texel shading and for GI bounce hit points.
    auto computeDirectAtPoint = [&](glm::vec3 pos, glm::vec3 N, glm::vec3 rayOrig, int skipTri) -> glm::vec3 {
        glm::vec3 direct(0.0f);
        for (const auto& li : lights) {
            glm::vec3 L;
            float atten = 1.0f;

            if (li.type == LightType::Directional) {
                L = glm::normalize(-li.direction);
            } else {
                glm::vec3 toL = li.position - pos;
                float dist = glm::length(toL);
                if (dist < 0.0001f) continue;
                L = toL / dist;
                if (li.range > 0.0f) {
                    float tt = glm::clamp(1.0f - dist / li.range, 0.0f, 1.0f);
                    atten = tt * tt;
                }
                if (atten < 0.001f) continue;
                if (li.type == LightType::Spot) {
                    float theta = glm::dot(L, -glm::normalize(li.direction));
                    float outerCos = cosf(glm::radians(li.outerAngle));
                    float innerCos = cosf(glm::radians(li.innerAngle));
                    float eps = innerCos - outerCos;
                    atten *= glm::clamp((theta - outerCos) / std::max(eps, 0.0001f), 0.0f, 1.0f);
                }
                if (atten < 0.001f) continue;
            }

            float NdotL = std::max(glm::dot(N, L), 0.0f);
            if (NdotL < 0.001f) continue;

            glm::vec3 shadowTarget = (li.type == LightType::Directional)
                ? rayOrig + L * 200.0f : li.position;
            float vis = ShadowRayTris(rayOrig, shadowTarget, worldTris, skipTri);
            direct += li.color * NdotL * atten * vis;
        }
        return direct;
    };

    size_t totalTris = 0;
    for (const auto& e : ents) if (e.w > 0 && e.h > 0) totalTris += (size_t)e.triCount;
    size_t doneTris = 0;

    emit(0.12f, "Baking texels");
    Rng rng;
    for (size_t ei = 0; ei < ents.size(); ++ei)
    {
        const auto& e = ents[ei];
        if (e.w <= 0 || e.h <= 0 || e.mesh == nullptr) continue;
        const auto& cv = e.mesh->GetCpuVertices();
        const int triCount = (int)cv.size() / 3;

        for (int t = 0; t < triCount; ++t)
        {
            const auto& v0 = cv[t * 3 + 0];
            const auto& v1 = cv[t * 3 + 1];
            const auto& v2 = cv[t * 3 + 2];

            glm::vec2 lmScale(e.so.x, e.so.y);
            glm::vec2 lmOffset(e.so.z, e.so.w);
            glm::vec2 uv0 = v0.uv2 * lmScale + lmOffset;
            glm::vec2 uv1 = v1.uv2 * lmScale + lmOffset;
            glm::vec2 uv2 = v2.uv2 * lmScale + lmOffset;

            glm::vec2 p0 = uv0 * glm::vec2((float)(W - 1), (float)(H - 1));
            glm::vec2 p1 = uv1 * glm::vec2((float)(W - 1), (float)(H - 1));
            glm::vec2 p2 = uv2 * glm::vec2((float)(W - 1), (float)(H - 1));

            int minX = std::max(0, (int)std::floor(std::min({p0.x, p1.x, p2.x})));
            int minY = std::max(0, (int)std::floor(std::min({p0.y, p1.y, p2.y})));
            int maxX = std::min(W - 1, (int)std::ceil(std::max({p0.x, p1.x, p2.x})));
            int maxY = std::min(H - 1, (int)std::ceil(std::max({p0.y, p1.y, p2.y})));

            for (int py = minY; py <= maxY; ++py)
            for (int px = minX; px <= maxX; ++px)
            {
                glm::vec2 p((float)px + 0.5f, (float)py + 0.5f);
                glm::vec3 bc(0.0f);
                if (!bary2(p, p0, p1, p2, bc)) continue;

                glm::vec3 localPos = v0.pos * bc.x + v1.pos * bc.y + v2.pos * bc.z;
                glm::vec3 localN = glm::normalize(v0.normal * bc.x + v1.normal * bc.y + v2.normal * bc.z);
                glm::vec3 worldPos = glm::vec3(e.world * glm::vec4(localPos, 1.0f));
                glm::vec3 N = glm::normalize(e.normalM * localN);

                // Skip the source triangle in all ray tests to prevent self-intersection.
                int srcTri = e.firstWorldTri + t;
                glm::vec3 rayOrig = worldPos + N * 0.02f;

                // ============================================================
                // 1. DIRECT LIGHTING — shadow-tested contribution from lights
                // ============================================================
                glm::vec3 direct = computeDirectAtPoint(worldPos, N, rayOrig, srcTri);

                // ============================================================
                // 2. AMBIENT OCCLUSION — hemisphere visibility ratio
                // ============================================================
                float ao = 0.0f;
                for (int s = 0; s < params.aoSamples; ++s) {
                    glm::vec3 dir = CosSampleHemi(N, rng.next(), rng.next());
                    if (!AORayTris(rayOrig, dir, params.aoRadius, worldTris, srcTri))
                        ao += 1.0f;
                }
                ao /= (float)std::max(params.aoSamples, 1);

                // ============================================================
                // 3. GLOBAL ILLUMINATION — Monte Carlo path-traced indirect
                //    Each sample traces a path through the scene. At each
                //    bounce the direct lighting at the hit point is
                //    accumulated, weighted by throughput (approximate BRDF).
                //    Rays that escape to sky accumulate the environment color.
                // ============================================================
                glm::vec3 indirect(0.0f);
                for (int s = 0; s < params.indirectSamples; ++s)
                {
                    glm::vec3 pathRad(0.0f);
                    glm::vec3 throughput(1.0f);
                    glm::vec3 curOrig = rayOrig;
                    glm::vec3 curN = N;
                    int curSkip = srcTri;

                    for (int bounce = 0; bounce < params.bounceCount; ++bounce)
                    {
                        glm::vec3 dir = CosSampleHemi(curN, rng.next(), rng.next());
                        RayHit hit = raycastClosest(curOrig, dir, 200.0f, curSkip);

                        if (hit.triIdx < 0) {
                            // Ray escaped to sky — environment contribution
                            pathRad += throughput * skyAt(dir);
                            break;
                        }

                        // Hit a surface: compute direct lighting at the hit point
                        glm::vec3 hitPos = curOrig + dir * hit.t;
                        glm::vec3 hitN = triNormals[hit.triIdx];
                        // Ensure normal faces the incoming ray
                        if (glm::dot(hitN, -dir) < 0.0f) hitN = -hitN;
                        glm::vec3 hitOrig = hitPos + hitN * 0.02f;

                        // Direct lighting at the bounce hit point
                        glm::vec3 hitDirect = computeDirectAtPoint(hitPos, hitN, hitOrig, hit.triIdx);
                        pathRad += throughput * hitDirect;

                        // Throughput decay: BRDF * cos / PDF = albedo for Lambertian
                        // Use 0.5 as an approximate average surface albedo
                        throughput *= 0.5f;

                        // Early termination for low-energy paths
                        float maxC = std::max({throughput.r, throughput.g, throughput.b});
                        if (maxC < 0.01f) break;

                        // Setup next bounce
                        curOrig = hitOrig;
                        curN = hitN;
                        curSkip = hit.triIdx;
                    }
                    indirect += pathRad;
                }
                indirect /= (float)std::max(params.indirectSamples, 1);

                // ============================================================
                // 4. FINAL IRRADIANCE
                //    Direct light has explicit shadow rays.
                //    Indirect (GI + sky) is modulated by AO for contact shadow.
                // ============================================================
                glm::vec3 col = direct + indirect * ao;
                int idx = py * W + px;
                hdr[idx] += col;
                samples[idx] += 1;
            }

            doneTris++;
            emit(0.12f + 0.80f * ((float)doneTris / (float)std::max<size_t>(1, totalTris)), "Baking texels");
        }
    }

    emit(0.93f, "Denoising atlas");
    for (size_t i = 0; i < hdr.size(); ++i)
        if (samples[i] > 0) hdr[i] /= (float)samples[i];

    if (params.denoiseRadius > 0) {
        const int r = params.denoiseRadius;
        std::vector<glm::vec3> tmp = hdr;
        for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            glm::vec3 sum(0.0f);
            int cnt = 0;
            for (int oy = -r; oy <= r; ++oy)
            for (int ox = -r; ox <= r; ++ox)
            {
                int sx = std::clamp(x + ox, 0, W - 1);
                int sy = std::clamp(y + oy, 0, H - 1);
                int si = sy * W + sx;
                if (samples[si] <= 0) continue;
                sum += tmp[si];
                cnt++;
            }
            if (cnt > 0) hdr[y * W + x] = sum / (float)cnt;
        }
    }

    // Expand valid texels into empty neighbors to prevent bilinear bleed/seam artifacts.
    // Cap to pad so dilation never crosses into an adjacent chart's territory.
    {
        const int dilatePasses = std::min(8, pad);
        std::vector<glm::vec3> tmp = hdr;
        std::vector<int> smp = samples;
        for (int pass = 0; pass < dilatePasses; ++pass) {
            bool any = false;
            for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                int idx = y * W + x;
                if (smp[idx] > 0) continue;

                glm::vec3 sum(0.0f);
                int cnt = 0;
                for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox)
                {
                    if (ox == 0 && oy == 0) continue;
                    int sx = x + ox;
                    int sy = y + oy;
                    if (sx < 0 || sx >= W || sy < 0 || sy >= H) continue;
                    int si = sy * W + sx;
                    if (smp[si] <= 0) continue;
                    sum += tmp[si];
                    cnt++;
                }

                if (cnt > 0) {
                    hdr[idx] = sum / (float)cnt;
                    samples[idx] = 1;
                    any = true;
                }
            }
            tmp = hdr;
            smp = samples;
            if (!any) break;
        }
    }

    // --- Diagnostic: compute bake statistics ---
    {
        int validTexels = 0;
        float minHdr = 1e30f, maxHdr = 0.0f, sumHdr = 0.0f;
        for (size_t i = 0; i < hdr.size(); ++i) {
            if (samples[i] <= 0) continue;
            validTexels++;
            float lum = hdr[i].r * 0.2126f + hdr[i].g * 0.7152f + hdr[i].b * 0.0722f;
            minHdr = std::min(minHdr, lum);
            maxHdr = std::max(maxHdr, lum);
            sumHdr += lum;
        }
        float avgHdr = validTexels > 0 ? sumHdr / validTexels : 0.0f;
        std::string diag = "Stats: " + std::to_string(validTexels) + " texels, HDR min="
                         + std::to_string(minHdr) + " max=" + std::to_string(maxHdr)
                         + " avg=" + std::to_string(avgHdr);
        emit(0.95f, diag.c_str());
    }

    out.pixels.assign(W * H * 3, 0);
    // Simple max normalization: maps the brightest baked texel to ~1.0,
    // preserving colour ratios across the whole atlas without percentile
    // artefacts that can crush contrast in complex scenes.
    float mx = 0.001f;
    for (size_t i = 0; i < hdr.size(); ++i) {
        if (samples[i] <= 0) continue;
        const glm::vec3& c = hdr[i];
        mx = std::max(mx, std::max(c.r, std::max(c.g, c.b)));
    }
    out.hdrMax = mx;  // store for runtime intensity restoration
    const float invNorm = 1.0f / mx;

    // Store linear HDR float pixels for GL_RGB16F upload (no gamma).
    out.hdrPixels.resize(W * H * 3, 0.0f);
    for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
    {
        int idx = y * W + x;
        glm::vec3 col = glm::clamp(hdr[idx] * invNorm, 0.0f, 1.0f);
        int di = idx * 3;
        out.hdrPixels[di + 0] = col.r;
        out.hdrPixels[di + 1] = col.g;
        out.hdrPixels[di + 2] = col.b;
    }

    // Also produce gamma-encoded RGB8 for TGA preview / debug file.
    for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
    {
        int idx = y * W + x;
        glm::vec3 col = glm::clamp(hdr[idx] * invNorm, 0.0f, 1.0f);
        col = glm::pow(col, glm::vec3(1.0f / 2.2f));

        int di = idx * 3;
        out.pixels[di + 0] = (unsigned char)(col.r * 255.0f + 0.5f);
        out.pixels[di + 1] = (unsigned char)(col.g * 255.0f + 0.5f);
        out.pixels[di + 2] = (unsigned char)(col.b * 255.0f + 0.5f);
    }

    emit(1.0f, "Bake complete");
    return out;
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
