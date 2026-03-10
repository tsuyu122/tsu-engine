#include "physics/physicsSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace tsu {

// ----------------------------------------------------------------
// BuildOBB — extracts position, rotation and scale from the world matrix
// ----------------------------------------------------------------

OBB PhysicsSystem::BuildOBB(const Scene& scene, uint32_t id)
{
    const auto& rb = scene.RigidBodies[id];
    glm::mat4 wm = scene.GetEntityWorldMatrix((int)id);

    glm::vec3 worldScale = {
        glm::length(glm::vec3(wm[0])),
        glm::length(glm::vec3(wm[1])),
        glm::length(glm::vec3(wm[2]))
    };

    OBB obb;
    obb.axes[0] = glm::normalize(glm::vec3(wm[0]));
    obb.axes[1] = glm::normalize(glm::vec3(wm[1]));
    obb.axes[2] = glm::normalize(glm::vec3(wm[2]));

    glm::vec3 entityPos = glm::vec3(wm[3]);
    obb.center = entityPos
        + obb.axes[0] * rb.ColliderOffset.x
        + obb.axes[1] * rb.ColliderOffset.y
        + obb.axes[2] * rb.ColliderOffset.z;

    obb.half = rb.ColliderSize * worldScale * 0.5f;

    return obb;
}

// ----------------------------------------------------------------
// CheckOBBOBB — SAT com 15 eixos separadores
// Normal aponta de B para A (empurra A para longe de B)
// ----------------------------------------------------------------

bool PhysicsSystem::CheckOBBOBB(
    const OBB& A, const OBB& B,
    glm::vec3& outNormal, float& outDepth)
{
    glm::mat3 R, AbsR;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            R[i][j]    = glm::dot(A.axes[i], B.axes[j]);
            AbsR[i][j] = std::abs(R[i][j]) + 1e-6f;
        }

    glm::vec3 T_world = B.center - A.center;
    glm::vec3 T = { glm::dot(T_world, A.axes[0]),
                    glm::dot(T_world, A.axes[1]),
                    glm::dot(T_world, A.axes[2]) };

    float minDepth = FLT_MAX;
    glm::vec3 bestAxis(0, 1, 0);
    bool flipNormal = false;

    auto test = [&](glm::vec3 axis, float rA, float rB, float dist) -> bool {
        float len2 = glm::dot(axis, axis);
        if (len2 < 1e-10f) return true;
        float invLen = 1.0f / std::sqrt(len2);
        axis *= invLen; rA *= invLen; rB *= invLen; dist *= invLen;
        float overlap = rA + rB - std::abs(dist);
        if (overlap <= 0.0f) return false;
        if (overlap < minDepth) {
            minDepth    = overlap;
            bestAxis    = axis;
            flipNormal  = (dist < 0.0f);
        }
        return true;
    };

    // Eixos de A
    for (int i = 0; i < 3; i++) {
        float rB = B.half[0]*AbsR[i][0] + B.half[1]*AbsR[i][1] + B.half[2]*AbsR[i][2];
        if (!test(A.axes[i], A.half[i], rB, T[i])) return false;
    }
    // Eixos de B
    for (int i = 0; i < 3; i++) {
        float rA = A.half[0]*AbsR[0][i] + A.half[1]*AbsR[1][i] + A.half[2]*AbsR[2][i];
        float d  = T[0]*R[0][i] + T[1]*R[1][i] + T[2]*R[2][i];
        if (!test(B.axes[i], rA, B.half[i], d)) return false;
    }
    // 9 eixos cruzados A[i] × B[j]
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            glm::vec3 cross = glm::cross(A.axes[i], B.axes[j]);
            int i1 = (i+1)%3, i2 = (i+2)%3;
            int j1 = (j+1)%3, j2 = (j+2)%3;
            float rA = A.half[i1]*AbsR[i2][j] + A.half[i2]*AbsR[i1][j];
            float rB = B.half[j1]*AbsR[i][j2] + B.half[j2]*AbsR[i][j1];
            float d  = T[i2]*R[i1][j] - T[i1]*R[i2][j];
            if (!test(cross, rA, rB, d)) return false;
        }
    }

    outDepth  = minDepth;
    outNormal = flipNormal ? -bestAxis : bestAxis;
    if (glm::dot(outNormal, A.center - B.center) < 0.0f)
        outNormal = -outNormal;
    return true;
}

// ----------------------------------------------------------------
// CheckSphereOBB — esfera vs OBB orientada
// ----------------------------------------------------------------

bool PhysicsSystem::CheckSphereOBB(
    const glm::vec3& spherePos, float radius,
    const OBB& box,
    glm::vec3& outNormal, float& outDepth)
{
    glm::vec3 d = spherePos - box.center;
    glm::vec3 local = {
        glm::dot(d, box.axes[0]),
        glm::dot(d, box.axes[1]),
        glm::dot(d, box.axes[2])
    };
    glm::vec3 closest = {
        std::max(-box.half.x, std::min(local.x, box.half.x)),
        std::max(-box.half.y, std::min(local.y, box.half.y)),
        std::max(-box.half.z, std::min(local.z, box.half.z))
    };
    glm::vec3 closestWorld = box.center
        + box.axes[0]*closest.x + box.axes[1]*closest.y + box.axes[2]*closest.z;

    glm::vec3 delta = spherePos - closestWorld;
    float dist = glm::length(delta);
    if (dist >= radius) return false;

    outDepth  = radius - dist;
    outNormal = dist > 1e-4f ? glm::normalize(delta) : box.axes[1];
    return true;
}

// ----------------------------------------------------------------
// CheckCapsuleOBB — capsule vs OBB (iterative closest point)
// ----------------------------------------------------------------

bool PhysicsSystem::CheckCapsuleOBB(
    const glm::vec3& capPos, float radius, float height,
    const glm::vec3& capAxisWorld,
    const OBB& box,
    glm::vec3& outNormal, float& outDepth)
{
    float halfH = std::max(height * 0.5f - radius, 0.0f);
    glm::vec3 capTop = capPos + capAxisWorld * halfH;
    glm::vec3 capBot = capPos - capAxisWorld * halfH;
    glm::vec3 seg = capTop - capBot;
    float segLen2 = glm::dot(seg, seg);

    // Find the closest point on the OBB to the capsule segment, and vice versa.
    // Iterate: project OBB closest point onto segment, then segment point onto OBB.
    glm::vec3 bestOnSeg = capPos;
    glm::vec3 bestOnBox = box.center;
    float bestDist2 = FLT_MAX;

    // Sample several points along the segment + iterate for convergence
    for (int s = 0; s <= 4; s++) {
        float t = s / 4.0f;
        glm::vec3 segPt = capBot + seg * t;

        // Find closest point on OBB to this segment point
        for (int iter = 0; iter < 3; iter++) {
            // Closest on OBB to segPt
            glm::vec3 d = segPt - box.center;
            glm::vec3 local = {
                glm::dot(d, box.axes[0]),
                glm::dot(d, box.axes[1]),
                glm::dot(d, box.axes[2])
            };
            glm::vec3 clamped = {
                std::max(-box.half.x, std::min(local.x, box.half.x)),
                std::max(-box.half.y, std::min(local.y, box.half.y)),
                std::max(-box.half.z, std::min(local.z, box.half.z))
            };
            glm::vec3 boxPt = box.center
                + box.axes[0]*clamped.x + box.axes[1]*clamped.y + box.axes[2]*clamped.z;

            // Project boxPt back onto segment
            float tSeg = (segLen2 > 1e-8f)
                ? glm::clamp(glm::dot(boxPt - capBot, seg) / segLen2, 0.0f, 1.0f)
                : 0.5f;
            segPt = capBot + seg * tSeg;
        }

        // Final closest on OBB
        glm::vec3 d = segPt - box.center;
        glm::vec3 local = {
            glm::dot(d, box.axes[0]),
            glm::dot(d, box.axes[1]),
            glm::dot(d, box.axes[2])
        };
        glm::vec3 clamped = {
            std::max(-box.half.x, std::min(local.x, box.half.x)),
            std::max(-box.half.y, std::min(local.y, box.half.y)),
            std::max(-box.half.z, std::min(local.z, box.half.z))
        };
        glm::vec3 boxPt = box.center
            + box.axes[0]*clamped.x + box.axes[1]*clamped.y + box.axes[2]*clamped.z;

        float dist2 = glm::dot(segPt - boxPt, segPt - boxPt);
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestOnSeg = segPt;
            bestOnBox = boxPt;
        }
    }

    float dist = std::sqrt(bestDist2);
    if (dist >= radius) return false;

    outDepth = radius - dist;
    glm::vec3 delta = bestOnSeg - bestOnBox;
    outNormal = dist > 1e-4f ? glm::normalize(delta) : box.axes[1];
    return true;
}

// ----------------------------------------------------------------
// CheckSphereSphere
// ----------------------------------------------------------------

bool PhysicsSystem::CheckSphereSphere(
    const glm::vec3& posA, float rA,
    const glm::vec3& posB, float rB,
    glm::vec3& outNormal, float& outDepth)
{
    float dist = glm::length(posB - posA);
    float sum  = rA + rB;
    if (dist >= sum) return false;
    outDepth  = sum - dist;
    outNormal = dist > 1e-4f ? glm::normalize(posA - posB) : glm::vec3(0, 1, 0);
    return true;
}

// ----------------------------------------------------------------
// CheckCapsuleSphere — closest point on capsule segment to sphere
// ----------------------------------------------------------------

bool PhysicsSystem::CheckCapsuleSphere(
    const glm::vec3& capPos, float capRadius, float capHeight,
    const glm::vec3& capAxisWorld,
    const glm::vec3& spherePos, float sphereRadius,
    glm::vec3& outNormal, float& outDepth)
{
    float halfH = std::max(capHeight * 0.5f - capRadius, 0.0f);
    glm::vec3 capA = capPos + capAxisWorld * halfH;
    glm::vec3 capB = capPos - capAxisWorld * halfH;
    glm::vec3 seg  = capA - capB;
    float segLen2  = glm::dot(seg, seg);
    float t = (segLen2 > 1e-8f)
        ? glm::clamp(glm::dot(spherePos - capB, seg) / segLen2, 0.0f, 1.0f)
        : 0.5f;
    glm::vec3 closest = capB + seg * t;
    return CheckSphereSphere(closest, capRadius, spherePos, sphereRadius, outNormal, outDepth);
}

// ----------------------------------------------------------------
// CheckCapsuleCapsule — closest points between two segments
// ----------------------------------------------------------------

static void ClosestPointsBetweenSegments(
    const glm::vec3& p1, const glm::vec3& q1,
    const glm::vec3& p2, const glm::vec3& q2,
    glm::vec3& c1, glm::vec3& c2)
{
    glm::vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    float a = glm::dot(d1, d1), e = glm::dot(d2, d2), f = glm::dot(d2, r);

    if (a <= 1e-8f && e <= 1e-8f) { c1 = p1; c2 = p2; return; }
    float s, t;
    if (a <= 1e-8f) { s = 0; t = glm::clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = glm::dot(d1, r);
        if (e <= 1e-8f) { t = 0; s = glm::clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > 1e-8f ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = glm::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1) { t = 1; s = glm::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    c1 = p1 + d1 * s;
    c2 = p2 + d2 * t;
}

bool PhysicsSystem::CheckCapsuleCapsule(
    const glm::vec3& capPosA, float rA, float hA, const glm::vec3& axisA,
    const glm::vec3& capPosB, float rB, float hB, const glm::vec3& axisB,
    glm::vec3& outNormal, float& outDepth)
{
    float halfHA = std::max(hA * 0.5f - rA, 0.0f);
    float halfHB = std::max(hB * 0.5f - rB, 0.0f);
    glm::vec3 a1 = capPosA + axisA * halfHA, a2 = capPosA - axisA * halfHA;
    glm::vec3 b1 = capPosB + axisB * halfHB, b2 = capPosB - axisB * halfHB;

    glm::vec3 c1, c2;
    ClosestPointsBetweenSegments(a1, a2, b1, b2, c1, c2);
    return CheckSphereSphere(c1, rA, c2, rB, outNormal, outDepth);
}

// ----------------------------------------------------------------
// BuildPyramidVerts — 5 world-space vertices: v0..v3 = base, v4 = apex
// ----------------------------------------------------------------

void PhysicsSystem::BuildPyramidVerts(const Scene& scene, uint32_t id, glm::vec3 outVerts[5])
{
    const auto& rb = scene.RigidBodies[id];
    glm::mat4 wm = scene.GetEntityWorldMatrix((int)id);

    glm::vec3 worldScale = {
        glm::length(glm::vec3(wm[0])),
        glm::length(glm::vec3(wm[1])),
        glm::length(glm::vec3(wm[2]))
    };
    glm::vec3 ax0 = glm::normalize(glm::vec3(wm[0]));
    glm::vec3 ax1 = glm::normalize(glm::vec3(wm[1]));
    glm::vec3 ax2 = glm::normalize(glm::vec3(wm[2]));

    glm::vec3 center = glm::vec3(wm[3])
        + ax0 * rb.ColliderOffset.x
        + ax1 * rb.ColliderOffset.y
        + ax2 * rb.ColliderOffset.z;

    glm::vec3 half = rb.ColliderSize * worldScale * 0.5f;

    // Base vertices (y = -halfY)
    outVerts[0] = center - ax0*half.x - ax1*half.y - ax2*half.z;
    outVerts[1] = center + ax0*half.x - ax1*half.y - ax2*half.z;
    outVerts[2] = center + ax0*half.x - ax1*half.y + ax2*half.z;
    outVerts[3] = center - ax0*half.x - ax1*half.y + ax2*half.z;
    // Apex (y = +halfY)
    outVerts[4] = center + ax1*half.y;
}

// ----------------------------------------------------------------
// ClosestPointOnTriangle — standard Voronoi region method
// ----------------------------------------------------------------

static glm::vec3 ClosestPointOnTriangle(const glm::vec3& p,
    const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    glm::vec3 bp = p - b;
    float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float denom = d1 - d3;
        float v = (std::abs(denom) > 1e-12f) ? d1 / denom : 0.5f;
        return a + ab * v;
    }

    glm::vec3 cp = p - c;
    float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float denom = d2 - d6;
        float w = (std::abs(denom) > 1e-12f) ? d2 / denom : 0.5f;
        return a + ac * w;
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float denom = (d4 - d3) + (d5 - d6);
        float w = (std::abs(denom) > 1e-12f) ? (d4 - d3) / denom : 0.5f;
        return b + (c - b) * w;
    }

    float denomFace = va + vb + vc;
    if (std::abs(denomFace) < 1e-12f) return a; // degenerate triangle
    float invDenom = 1.0f / denomFace;
    float v = vb * invDenom, w = vc * invDenom;
    return a + ab * v + ac * w;
}

// ----------------------------------------------------------------
// CheckPyramidOBB — SAT with pyramid face-normals
// Pyramid: v0..v3 base, v4 apex. Faces: base + 4 lateral triangles.
// ----------------------------------------------------------------

bool PhysicsSystem::CheckPyramidOBB(
    const glm::vec3 pyrVerts[5],
    const OBB& box,
    glm::vec3& outNormal, float& outDepth)
{
    // Compute pyramid face normals
    glm::vec3 pyrNormals[5];
    // Base face: (v0,v1,v2,v3) — outward normal pointing down
    pyrNormals[0] = glm::normalize(glm::cross(pyrVerts[1] - pyrVerts[0], pyrVerts[3] - pyrVerts[0]));
    // 4 lateral faces
    pyrNormals[1] = glm::normalize(glm::cross(pyrVerts[1] - pyrVerts[0], pyrVerts[4] - pyrVerts[0]));
    pyrNormals[2] = glm::normalize(glm::cross(pyrVerts[2] - pyrVerts[1], pyrVerts[4] - pyrVerts[1]));
    pyrNormals[3] = glm::normalize(glm::cross(pyrVerts[3] - pyrVerts[2], pyrVerts[4] - pyrVerts[2]));
    pyrNormals[4] = glm::normalize(glm::cross(pyrVerts[0] - pyrVerts[3], pyrVerts[4] - pyrVerts[3]));

    // Unique pyramid edge directions (2 base + 4 lateral = 6)
    glm::vec3 pyrEdges[6];
    pyrEdges[0] = pyrVerts[1] - pyrVerts[0]; // base X
    pyrEdges[1] = pyrVerts[3] - pyrVerts[0]; // base Z
    pyrEdges[2] = pyrVerts[4] - pyrVerts[0]; // lateral 0
    pyrEdges[3] = pyrVerts[4] - pyrVerts[1]; // lateral 1
    pyrEdges[4] = pyrVerts[4] - pyrVerts[2]; // lateral 2
    pyrEdges[5] = pyrVerts[4] - pyrVerts[3]; // lateral 3

    // Pyramid centroid for normal direction
    glm::vec3 pyrCenter = (pyrVerts[0] + pyrVerts[1] + pyrVerts[2] + pyrVerts[3] + pyrVerts[4]) * 0.2f;

    float minDepth = FLT_MAX;
    glm::vec3 bestAxis(0, 1, 0);

    auto testAxis = [&](glm::vec3 axis) -> bool {
        float len2 = glm::dot(axis, axis);
        if (len2 < 1e-10f) return true;
        axis /= std::sqrt(len2);

        // Project pyramid (5 verts)
        float pyrMin = FLT_MAX, pyrMax = -FLT_MAX;
        for (int i = 0; i < 5; i++) {
            float p = glm::dot(pyrVerts[i], axis);
            pyrMin = std::min(pyrMin, p);
            pyrMax = std::max(pyrMax, p);
        }

        // Project OBB
        float obbC = glm::dot(box.center, axis);
        float obbE = std::abs(glm::dot(box.axes[0], axis)) * box.half[0]
                   + std::abs(glm::dot(box.axes[1], axis)) * box.half[1]
                   + std::abs(glm::dot(box.axes[2], axis)) * box.half[2];

        float overlap = std::min(pyrMax, obbC + obbE) - std::max(pyrMin, obbC - obbE);
        if (overlap <= 0.0f) return false;
        if (overlap < minDepth) {
            minDepth = overlap;
            bestAxis = axis;
            // Normal points from OBB to pyramid
            if (glm::dot(pyrCenter - box.center, bestAxis) < 0.0f)
                bestAxis = -bestAxis;
        }
        return true;
    };

    // Test 5 pyramid face normals
    for (int i = 0; i < 5; i++)
        if (!testAxis(pyrNormals[i])) return false;

    // Test 3 OBB face normals
    for (int i = 0; i < 3; i++)
        if (!testAxis(box.axes[i])) return false;

    // Test 6×3 = 18 edge cross products
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 3; j++)
            if (!testAxis(glm::cross(pyrEdges[i], box.axes[j]))) return false;

    outDepth  = minDepth;
    outNormal = bestAxis;
    return true;
}

// ----------------------------------------------------------------
// CheckSpherePyramid — closest point on pyramid surface to sphere
// ----------------------------------------------------------------

bool PhysicsSystem::CheckSpherePyramid(
    const glm::vec3& spherePos, float radius,
    const glm::vec3 pyrVerts[5],
    glm::vec3& outNormal, float& outDepth)
{
    // 6 triangles: 4 lateral + 2 from base quad split
    const int faces[6][3] = {
        {0,1,4}, {1,2,4}, {2,3,4}, {3,0,4},  // lateral
        {0,1,2}, {0,2,3}                       // base quad
    };

    float minDist2 = FLT_MAX;
    glm::vec3 closestPt(0.0f);

    for (int f = 0; f < 6; f++) {
        glm::vec3 cp = ClosestPointOnTriangle(spherePos,
            pyrVerts[faces[f][0]], pyrVerts[faces[f][1]], pyrVerts[faces[f][2]]);
        float d2 = glm::dot(spherePos - cp, spherePos - cp);
        if (d2 < minDist2) { minDist2 = d2; closestPt = cp; }
    }

    float dist = std::sqrt(minDist2);
    if (dist >= radius) return false;

    glm::vec3 delta = spherePos - closestPt;
    outDepth  = radius - dist;
    outNormal = dist > 1e-4f ? glm::normalize(delta) : glm::vec3(0, 1, 0);
    return true;
}

// ----------------------------------------------------------------
// CheckCapsulePyramid — closest on capsule segment → sphere-pyramid
// ----------------------------------------------------------------

bool PhysicsSystem::CheckCapsulePyramid(
    const glm::vec3& capPos, float radius, float height,
    const glm::vec3& capAxisWorld,
    const glm::vec3 pyrVerts[5],
    glm::vec3& outNormal, float& outDepth)
{
    // Sample multiple points along the capsule segment, keep deepest hit
    float halfH = std::max(height * 0.5f - radius, 0.0f);
    glm::vec3 capTop = capPos + capAxisWorld * halfH;
    glm::vec3 capBot = capPos - capAxisWorld * halfH;

    bool anyHit = false;
    float bestDepth = 0.0f;
    glm::vec3 bestNormal(0, 1, 0);

    // 5 sample points along segment
    for (int s = 0; s <= 4; s++) {
        float t = s / 4.0f;
        glm::vec3 pt = capBot + (capTop - capBot) * t;
        glm::vec3 n; float d;
        if (CheckSpherePyramid(pt, radius, pyrVerts, n, d)) {
            if (!anyHit || d > bestDepth) {
                bestDepth = d;
                bestNormal = n;
                anyHit = true;
            }
        }
    }

    if (anyHit) { outDepth = bestDepth; outNormal = bestNormal; }
    return anyHit;
}

// ----------------------------------------------------------------
// CheckPyramidPyramid — SAT between two pyramids
// ----------------------------------------------------------------

bool PhysicsSystem::CheckPyramidPyramid(
    const glm::vec3 vertsA[5], const glm::vec3 vertsB[5],
    glm::vec3& outNormal, float& outDepth)
{
    // Build face normals for both pyramids
    auto buildNormals = [](const glm::vec3 v[5], glm::vec3 n[5]) {
        n[0] = glm::normalize(glm::cross(v[1] - v[0], v[3] - v[0]));
        n[1] = glm::normalize(glm::cross(v[1] - v[0], v[4] - v[0]));
        n[2] = glm::normalize(glm::cross(v[2] - v[1], v[4] - v[1]));
        n[3] = glm::normalize(glm::cross(v[3] - v[2], v[4] - v[2]));
        n[4] = glm::normalize(glm::cross(v[0] - v[3], v[4] - v[3]));
    };
    auto buildEdges = [](const glm::vec3 v[5], glm::vec3 e[6]) {
        e[0] = v[1] - v[0]; e[1] = v[3] - v[0];
        e[2] = v[4] - v[0]; e[3] = v[4] - v[1];
        e[4] = v[4] - v[2]; e[5] = v[4] - v[3];
    };

    glm::vec3 nA[5], nB[5], eA[6], eB[6];
    buildNormals(vertsA, nA); buildNormals(vertsB, nB);
    buildEdges(vertsA, eA);   buildEdges(vertsB, eB);

    glm::vec3 centA = (vertsA[0]+vertsA[1]+vertsA[2]+vertsA[3]+vertsA[4])*0.2f;
    glm::vec3 centB = (vertsB[0]+vertsB[1]+vertsB[2]+vertsB[3]+vertsB[4])*0.2f;

    float minDepth = FLT_MAX;
    glm::vec3 bestAxis(0,1,0);

    auto testAxis = [&](glm::vec3 axis) -> bool {
        float len2 = glm::dot(axis, axis);
        if (len2 < 1e-10f) return true;
        axis /= std::sqrt(len2);
        float aMin=FLT_MAX, aMax=-FLT_MAX, bMin=FLT_MAX, bMax=-FLT_MAX;
        for (int i=0;i<5;i++) {
            float pa=glm::dot(vertsA[i],axis); aMin=std::min(aMin,pa); aMax=std::max(aMax,pa);
            float pb=glm::dot(vertsB[i],axis); bMin=std::min(bMin,pb); bMax=std::max(bMax,pb);
        }
        float overlap = std::min(aMax,bMax) - std::max(aMin,bMin);
        if (overlap<=0.0f) return false;
        if (overlap<minDepth) {
            minDepth=overlap; bestAxis=axis;
            if (glm::dot(centA-centB,bestAxis)<0.0f) bestAxis=-bestAxis;
        }
        return true;
    };

    for (int i=0;i<5;i++) if(!testAxis(nA[i])) return false;
    for (int i=0;i<5;i++) if(!testAxis(nB[i])) return false;
    for (int i=0;i<6;i++) for(int j=0;j<6;j++)
        if(!testAxis(glm::cross(eA[i],eB[j]))) return false;

    outDepth=minDepth; outNormal=bestAxis;
    return true;
}

// ----------------------------------------------------------------
// Helpers for full RigidBody resolution
// ----------------------------------------------------------------

// Inverse of box principal moments of inertia (diagonal)
static glm::vec3 BoxInertiaDiagInv(float mass, const glm::vec3& half)
{
    return glm::vec3(
        3.0f / std::max(mass * (half.y*half.y + half.z*half.z), 0.01f),
        3.0f / std::max(mass * (half.x*half.x + half.z*half.z), 0.01f),
        3.0f / std::max(mass * (half.x*half.x + half.y*half.y), 0.01f)
    );
}

// Inverse of solid sphere moments of inertia: I = (2/5)*m*r²
static glm::vec3 SphereInertiaDiagInv(float mass, float radius)
{
    float I = 0.4f * mass * radius * radius;
    float inv = 1.0f / std::max(I, 0.01f);
    return glm::vec3(inv);
}

// Inverse of capsule moments of inertia (approx. solid cylinder)
// Y axis = cylinder axis
static glm::vec3 CapsuleInertiaDiagInv(float mass, float radius, float height)
{
    float r2 = radius * radius;
    float Iaxial = mass * r2 * 0.5f;
    float Iperp  = mass * (3.0f * r2 + height * height) / 12.0f;
    return glm::vec3(
        1.0f / std::max(Iperp,  0.01f),
        1.0f / std::max(Iaxial, 0.01f),
        1.0f / std::max(Iperp,  0.01f)
    );
}

// Inverse of solid pyramid moments of inertia (square base)
// CM at H/4 from base. Iy = m*a²/10, Ix=Iz = m*(a²/20 + 3h²/80)
static glm::vec3 PyramidInertiaDiagInv(float mass, const glm::vec3& half)
{
    float a2 = (2.0f*half.x) * (2.0f*half.z); // base area proxy
    float h2 = (2.0f*half.y) * (2.0f*half.y);
    float Iy = mass * a2 / 10.0f;
    float Ixz = mass * (a2 / 20.0f + 3.0f * h2 / 80.0f);
    return glm::vec3(
        1.0f / std::max(Ixz, 0.01f),
        1.0f / std::max(Iy,  0.01f),
        1.0f / std::max(Ixz, 0.01f)
    );
}

// Applies I^{-1} diagonal (in OBB local space) to a world-space vector
static glm::vec3 ApplyIInvWorld(const OBB& obb, const glm::vec3& Iinv, const glm::vec3& v)
{
    glm::vec3 local = {
        glm::dot(v, obb.axes[0]),
        glm::dot(v, obb.axes[1]),
        glm::dot(v, obb.axes[2])
    };
    local *= Iinv;
    return obb.axes[0]*local.x + obb.axes[1]*local.y + obb.axes[2]*local.z;
}

// Centroid of the OBB vertices most extreme in direction `dir`.
// Handles corner (1 vertex), edge (2) and face (4) contacts correctly:
// by averaging the tied vertices the "along-the-edge" component
// cancels out, avoiding spurious torques on axes not involved in the collision.
static glm::vec3 OBBContactCentroid(const OBB& obb, const glm::vec3& dir)
{
    float tol = 1e-3f * (obb.half.x + obb.half.y + obb.half.z);

    // 1st pass: MAXIMUM projection (most extreme vertex in direction `dir`)
    float maxProj = -FLT_MAX;
    for (int mask = 0; mask < 8; mask++) {
        float sx = (mask & 1) ? 1.0f : -1.0f;
        float sy = (mask & 2) ? 1.0f : -1.0f;
        float sz = (mask & 4) ? 1.0f : -1.0f;
        glm::vec3 c = obb.center
            + obb.axes[0] * obb.half[0] * sx
            + obb.axes[1] * obb.half[1] * sy
            + obb.axes[2] * obb.half[2] * sz;
        maxProj = std::max(maxProj, glm::dot(c, dir));
    }

    // 2nd pass: average of all vertices within tolerance
    glm::vec3 sum(0.0f);
    int count = 0;
    for (int mask = 0; mask < 8; mask++) {
        float sx = (mask & 1) ? 1.0f : -1.0f;
        float sy = (mask & 2) ? 1.0f : -1.0f;
        float sz = (mask & 4) ? 1.0f : -1.0f;
        glm::vec3 c = obb.center
            + obb.axes[0] * obb.half[0] * sx
            + obb.axes[1] * obb.half[1] * sy
            + obb.axes[2] * obb.half[2] * sz;
        if (glm::dot(c, dir) >= maxProj - tol) {
            sum += c;
            count++;
        }
    }
    return (count > 0) ? sum / (float)count : obb.center;
}

void PhysicsSystem::Update(Scene& scene, float dt)
{
    const float MAX_FALL_SPEED = -50.0f;

    for (size_t i = 0; i < scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];

        if (i < scene.GameCameras.size() && scene.GameCameras[i].Active)
            continue;

        bool isPhysics = rb.HasGravityModule || rb.HasRigidBodyMode;
        if (!isPhysics) continue;
        if (rb.IsKinematic) continue;

        if (rb.UseGravity)
        {
            rb.Velocity.y += Gravity * rb.FallSpeedMultiplier * dt;
            rb.Velocity.y  = std::max(rb.Velocity.y, MAX_FALL_SPEED * rb.FallSpeedMultiplier);
        }

        rb.Velocity.x *= (1.0f - rb.Drag);
        rb.Velocity.z *= (1.0f - rb.Drag);

        scene.Transforms[i].Position += rb.Velocity * dt;

        if (rb.HasRigidBodyMode)
        {
            scene.Transforms[i].Rotation += rb.AngularVelocity * dt;
            rb.AngularVelocity *= (1.0f - rb.AngularDamping);
        }

        rb.IsGrounded = false;
    }

    for (int iter = 0; iter < 4; ++iter)
        ResolveCollisions(scene);

    // Direct gravity torque for RigidBody in contact with the ground.
    // Executed after ResolveCollisions to use freshly-updated IsGrounded,
    // ensuring immediate angular response on the first contact frame.
    // τ = arm × N_contact, where N_contact ≈ (0, mass*|g|, 0) balances gravity.
    // For tilt < 45° the torque is restoring (returns to flat);
    // for tilt > 45° the torque tips the object over.
    // Sphere does NOT enter this loop (rotationally symmetric — doesn’t tip).
    for (size_t i = 0; i < scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];
        if (!rb.HasRigidBodyMode || !rb.HasColliderModule) continue;
        if (rb.IsKinematic || !rb.UseGravity || !rb.IsGrounded) continue;

        // Spheres and capsules don't use the tipping model (pivot at contact).
        // Spheres: rotationally symmetric — don't tip.
        // Capsules: rounded bottom — rolling comes from collision friction,
        // not gravitational torque. If they entered here, the pendulum
        // constraint (velocity = cross(omega, -arm)) would overwrite the
        // tangential ramp velocity, making the capsule stop like a box.
        if (rb.Collider == ColliderType::Sphere) continue;
        if (rb.Collider == ColliderType::Capsule) continue;

        OBB obb = BuildOBB(scene, (uint32_t)i);

        glm::vec3 contact;
        if (rb.Collider == ColliderType::Pyramid)
        {
        // Use real pyramid vertices to find contact point
            glm::vec3 pyrV[5];
            BuildPyramidVerts(scene, (uint32_t)i, pyrV);
            float tol = 1e-3f * (obb.half.x + obb.half.y + obb.half.z);
            glm::vec3 downDir(0.0f, -1.0f, 0.0f);
            float maxP = -FLT_MAX;
            for (int k = 0; k < 5; k++)
                maxP = std::max(maxP, glm::dot(pyrV[k], downDir));
            glm::vec3 sum(0); int cnt = 0;
            for (int k = 0; k < 5; k++) {
                if (glm::dot(pyrV[k], downDir) >= maxP - tol) { sum += pyrV[k]; cnt++; }
            }
            contact = sum / (float)cnt;
        }
        else
        {
            // Box: lowest OBB vertex(es)
            contact = OBBContactCentroid(obb, glm::vec3(0.0f, -1.0f, 0.0f));
        }

        // Use actual enter of mass (pyramid CM is at H/4 from base, not H/2)
        glm::vec3 cm = obb.center;
        if (rb.Collider == ColliderType::Pyramid)
            cm = obb.center - obb.axes[1] * (obb.half.y * 0.5f);
        glm::vec3 arm = contact - cm; // CM → contact

        // If arm is nearly vertical → flat or exactly 45°
        float armHorizSq = arm.x*arm.x + arm.z*arm.z;
        if (armHorizSq < 0.0001f)
        {
            // Distinguish flat cube (settled) from cube stuck at 45°
            const auto& rot = scene.Transforms[i].Rotation;
            auto near45 = [](float deg) {
                float d = std::fmod(std::abs(deg), 90.0f);
                return d > 30.0f && d < 60.0f;
            };
            if ((near45(rot.x) || near45(rot.z)) &&
                glm::length(rb.AngularVelocity) < 5.0f)
            {
                // Stuck at 45° — randomly nudge to one side (50/50)
                static uint32_t rng45 = 0xDEADBEEFu;
                rng45 = rng45 * 1664525u + 1013904223u;
                float sx = (rng45 & 0x80000000u) ? 1.0f : -1.0f;
                float sz = (rng45 & 0x40000000u) ? 1.0f : -1.0f;
                rb.AngularVelocity.x += sx * 30.0f;
                rb.AngularVelocity.z += sz * 15.0f;
            }
            else if (!near45(rot.x) && !near45(rot.z))
            {
                // Truly flat — decelerate and stop
                rb.AngularVelocity *= 0.8f;
                if (glm::length(rb.AngularVelocity) < 0.5f)
                    rb.AngularVelocity = glm::vec3(0);
                rb.Velocity.x = 0.0f;
                rb.Velocity.z = 0.0f;
            }
            continue;
        }

        // Torque: τ = arm × N_contact, N = (0, +mg, 0)
        glm::vec3 tau = glm::cross(arm, glm::vec3(0.0f, rb.Mass * std::abs(Gravity), 0.0f));

        // Arm in OBB local space (for Steiner)
        glm::vec3 armL = {
            glm::dot(arm, obb.axes[0]),
            glm::dot(arm, obb.axes[1]),
            glm::dot(arm, obb.axes[2])
        };

        // CM inertia (diagonal) — per-shape
        glm::vec3 IcmInv;
        glm::vec3 Icm;
        if (rb.Collider == ColliderType::Pyramid) {
            IcmInv = PyramidInertiaDiagInv(rb.Mass, obb.half);
            Icm = glm::vec3(1.0f / std::max(IcmInv.x, 0.01f),
                            1.0f / std::max(IcmInv.y, 0.01f),
                            1.0f / std::max(IcmInv.z, 0.01f));
        } else {
            Icm = glm::vec3(
                std::max(rb.Mass * (obb.half.y*obb.half.y + obb.half.z*obb.half.z) / 3.0f, 0.01f),
                std::max(rb.Mass * (obb.half.x*obb.half.x + obb.half.z*obb.half.z) / 3.0f, 0.01f),
                std::max(rb.Mass * (obb.half.x*obb.half.x + obb.half.y*obb.half.y) / 3.0f, 0.01f)
            );
        }

        // Steiner: I_pivot = I_cm + m * |arm_perp|²
        glm::vec3 Ipivot = {
            Icm.x + rb.Mass * (armL.y*armL.y + armL.z*armL.z),
            Icm.y + rb.Mass * (armL.x*armL.x + armL.z*armL.z),
            Icm.z + rb.Mass * (armL.x*armL.x + armL.y*armL.y)
        };
        glm::vec3 IpivotInv = {
            1.0f / std::max(Ipivot.x, 0.01f),
            1.0f / std::max(Ipivot.y, 0.01f),
            1.0f / std::max(Ipivot.z, 0.01f)
        };

        // Angular acceleration (rad/s²) using pivot inertia
        glm::vec3 dOmegaRad = ApplyIInvWorld(obb, IpivotInv, tau) * dt;

        // Clamp angular acceleration to prevent explosion from vertex jumps
        float dOmegaLen = glm::length(dOmegaRad);
        float maxDOmega = glm::radians(360.0f) * dt; // max 360°/s² acceleration
        if (dOmegaLen > maxDOmega)
            dOmegaRad *= maxDOmega / dOmegaLen;

        rb.AngularVelocity += glm::degrees(dOmegaRad);

        float spd = glm::length(rb.AngularVelocity);
        if (spd > 720.0f) rb.AngularVelocity *= 720.0f / spd;

        // Gravity never produces Y torque: arm × (0,mg,0) → tau.y ≡ 0.
        // Any AngularVelocity.y when grounded comes from collision impulses
        // or numerical coupling from the multi-axis cross-product.
        // We zero it completely to eliminate spurious horizontal spin.
        rb.AngularVelocity.y = 0.0f;

        // PENDULUM CONSTRAINT: v_cm = ω_total × (-arm)
        glm::vec3 omegaRad = glm::radians(rb.AngularVelocity);
        glm::vec3 vPendulum = glm::cross(omegaRad, -arm);

        // Limit total pendulum speed
        float armLen = glm::length(arm);
        float maxPendulumSpeed = std::max(armLen * glm::radians(720.0f), 15.0f);
        float pendSpeed = glm::length(vPendulum);
        if (pendSpeed > maxPendulumSpeed)
            vPendulum *= maxPendulumSpeed / pendSpeed;

        // Apply X and Z from pendulum; Y only if downward.
        //
        // Why clamp Y:
        //   In multi-axis tilts (X+Y, X+Z), cross(ω,-arm).y can be positive
        //   because ω accumulates X and Z components while arm has Z component:
        //     vPendulum.y = ω.x*arm.z - ω.z*arm.x
        //   Positive → launches object upward → jumps off ground → bounces.
        //
        // Apply pendulum constraint: X and Z directly, Y only downward.
        // maxDOmega already limits abrupt ω changes, so we don't need
        // to smooth X/Z here (excessive smoothing locked the cube).
        rb.Velocity.x = vPendulum.x;
        rb.Velocity.z = vPendulum.z;
        rb.Velocity.y = std::min(vPendulum.y, 0.0f);
    }

    // Capsule standing upright: unstable equilibrium → perturb to tip over.
    // A real capsule standing upright (height > diameter) tips with any perturbation.
    // The hemispherical bottom keeps the contact directly below the hemisphere center,
    // so the gravity torque loop generates no torque (arm is vertical).
    // Solution: detect upright capsule and apply angular perturbation.
    for (size_t i = 0; i < scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];
        if (!rb.HasRigidBodyMode || !rb.HasColliderModule) continue;
        if (rb.Collider != ColliderType::Capsule) continue;
        if (rb.IsKinematic || !rb.IsGrounded) continue;

        glm::mat4 wm = scene.GetEntityWorldMatrix((int)i);
        glm::vec3 capAxis = glm::normalize(glm::vec3(wm[1]));
        float axisVert = std::abs(capAxis.y);

        float capRadius = rb.ColliderRadius * glm::length(glm::vec3(wm[0]));
        float capHeight = rb.ColliderHeight * glm::length(glm::vec3(wm[1]));
        float capHalfH = std::max(capHeight * 0.5f - capRadius, 0.0f);

        // Capsule standing (axis nearly vertical, segment > 0)
        if (axisVert > 0.85f && capHalfH > 0.01f)
        {
            // Apply perturbation once to kickstart tipping
            if (glm::length(rb.AngularVelocity) < 3.0f)
            {
                rb.AngularVelocity.x += 8.0f;
                rb.AngularVelocity.z += 5.0f;
            }
        }
    }

    // Cancel residual gravity velocity for objects resting on the ground.
    // Does NOT apply to tipping objects (significant ω) — those need
    // vertical velocity to orbit the contact point.
    for (size_t i = 0; i < scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];
        if (rb.IsKinematic) continue;
        if (!rb.HasGravityModule && !rb.HasRigidBodyMode) continue;
        if (rb.IsGrounded && rb.Velocity.y < 0.1f)
        {
            bool tipping = rb.HasRigidBodyMode && glm::length(rb.AngularVelocity) > 1.0f;
            // Spheres on ramps need to keep Velocity.y to roll along the slope.
            // Collision already handles the normal component; zeroing Y here kills tangential velocity.
            bool isSphere = rb.HasColliderModule && rb.Collider == ColliderType::Sphere;
            bool isCapsule = rb.HasColliderModule && rb.Collider == ColliderType::Capsule;
            if (!tipping && !isSphere && !isCapsule) rb.Velocity.y = 0.0f;
        }
    }
}

// ----------------------------------------------------------------
// ResolveCollisions — OBB for box/pyramid, sphere/capsule correct
// ----------------------------------------------------------------

void PhysicsSystem::ResolveCollisions(Scene& scene)
{
    size_t n = scene.RigidBodies.size();

    for (size_t i = 0; i < n; i++)
    {
        auto& rbA = scene.RigidBodies[i];
        if (!rbA.HasColliderModule) continue;

        for (size_t j = i + 1; j < n; j++)
        {
            auto& rbB = scene.RigidBodies[j];
            if (!rbB.HasColliderModule) continue;
            if (rbA.IsKinematic && rbB.IsKinematic) continue;

            bool aKin = rbA.IsKinematic || (!rbA.HasGravityModule && !rbA.HasRigidBodyMode);
            bool bKin = rbB.IsKinematic || (!rbB.HasGravityModule && !rbB.HasRigidBodyMode);

            ColliderType tA = rbA.Collider;
            ColliderType tB = rbB.Collider;

            bool aIsBox = (tA == ColliderType::Box);
            bool bIsBox = (tB == ColliderType::Box);
            bool aIsPyr = (tA == ColliderType::Pyramid);
            bool bIsPyr = (tB == ColliderType::Pyramid);

            OBB obbA, obbB;
            if (aIsBox) obbA = BuildOBB(scene, (uint32_t)i);
            if (bIsBox) obbB = BuildOBB(scene, (uint32_t)j);

            glm::vec3 pyrVertsA[5], pyrVertsB[5];
            if (aIsPyr) BuildPyramidVerts(scene, (uint32_t)i, pyrVertsA);
            if (bIsPyr) BuildPyramidVerts(scene, (uint32_t)j, pyrVertsB);

            glm::mat4 wmA = scene.GetEntityWorldMatrix((int)i);
            glm::mat4 wmB = scene.GetEntityWorldMatrix((int)j);
            glm::vec3 scaleA = { glm::length(glm::vec3(wmA[0])), glm::length(glm::vec3(wmA[1])), glm::length(glm::vec3(wmA[2])) };
            glm::vec3 scaleB = { glm::length(glm::vec3(wmB[0])), glm::length(glm::vec3(wmB[1])), glm::length(glm::vec3(wmB[2])) };
            glm::vec3 posA = glm::vec3(wmA[3]) + rbA.ColliderOffset;
            glm::vec3 posB = glm::vec3(wmB[3]) + rbB.ColliderOffset;
            float rA = rbA.ColliderRadius * scaleA.x;
            float rB = rbB.ColliderRadius * scaleB.x;
            float hA = rbA.ColliderHeight * scaleA.y;
            float hB = rbB.ColliderHeight * scaleB.y;
            glm::vec3 capAxisA = glm::normalize(glm::vec3(wmA[1]));
            glm::vec3 capAxisB = glm::normalize(glm::vec3(wmB[1]));

            glm::vec3 normal(0);
            float     depth = 0;
            bool      hit   = false;

            // --- Box vs Box ---
            if (aIsBox && bIsBox)
                hit = CheckOBBOBB(obbA, obbB, normal, depth);

            // --- Box vs Pyramid ---
            else if (aIsBox && bIsPyr)
                { hit = CheckPyramidOBB(pyrVertsB, obbA, normal, depth); normal = -normal; }
            else if (aIsPyr && bIsBox)
                hit = CheckPyramidOBB(pyrVertsA, obbB, normal, depth);

            // --- Box vs Sphere ---
            else if (aIsBox && tB == ColliderType::Sphere)
                { hit = CheckSphereOBB(posB, rB, obbA, normal, depth); normal = -normal; }
            else if (tA == ColliderType::Sphere && bIsBox)
                hit = CheckSphereOBB(posA, rA, obbB, normal, depth);

            // --- Box vs Capsule ---
            else if (aIsBox && tB == ColliderType::Capsule)
                { hit = CheckCapsuleOBB(posB, rB, hB, capAxisB, obbA, normal, depth); normal = -normal; }
            else if (tA == ColliderType::Capsule && bIsBox)
                hit = CheckCapsuleOBB(posA, rA, hA, capAxisA, obbB, normal, depth);

            // --- Pyramid vs Pyramid ---
            else if (aIsPyr && bIsPyr)
                hit = CheckPyramidPyramid(pyrVertsA, pyrVertsB, normal, depth);

            // --- Pyramid vs Sphere ---
            else if (aIsPyr && tB == ColliderType::Sphere)
                { hit = CheckSpherePyramid(posB, rB, pyrVertsA, normal, depth); normal = -normal; }
            else if (tA == ColliderType::Sphere && bIsPyr)
                hit = CheckSpherePyramid(posA, rA, pyrVertsB, normal, depth);

            // --- Pyramid vs Capsule ---
            else if (aIsPyr && tB == ColliderType::Capsule)
                { hit = CheckCapsulePyramid(posB, rB, hB, capAxisB, pyrVertsA, normal, depth); normal = -normal; }
            else if (tA == ColliderType::Capsule && bIsPyr)
                hit = CheckCapsulePyramid(posA, rA, hA, capAxisA, pyrVertsB, normal, depth);

            // --- Sphere vs Sphere ---
            else if (tA == ColliderType::Sphere && tB == ColliderType::Sphere)
                hit = CheckSphereSphere(posA, rA, posB, rB, normal, depth);

            // --- Capsule vs Sphere ---
            else if (tA == ColliderType::Capsule && tB == ColliderType::Sphere)
                hit = CheckCapsuleSphere(posA, rA, hA, capAxisA, posB, rB, normal, depth);
            else if (tA == ColliderType::Sphere && tB == ColliderType::Capsule)
                { hit = CheckCapsuleSphere(posB, rB, hB, capAxisB, posA, rA, normal, depth); normal = -normal; }

            // --- Capsule vs Capsule ---
            else if (tA == ColliderType::Capsule && tB == ColliderType::Capsule)
                hit = CheckCapsuleCapsule(posA, rA, hA, capAxisA, posB, rB, hB, capAxisB, normal, depth);

            if (!hit) continue;

            // --- Positional separation: Baumgarte with slop ---
            constexpr float SLOP      = 0.005f;
            constexpr float BAUMGARTE = 0.6f;
            float corr = std::max(depth - SLOP, 0.0f) * BAUMGARTE;

            if (!aKin && !bKin)
            { scene.Transforms[i].Position += normal*(corr*0.5f); scene.Transforms[j].Position -= normal*(corr*0.5f); }
            else if (!aKin)
                scene.Transforms[i].Position += normal * corr;
            else if (!bKin)
                scene.Transforms[j].Position -= normal * corr;

            // --- Velocity resolution ---
            bool anyRB = rbA.HasRigidBodyMode || rbB.HasRigidBodyMode;

            if (anyRB)
            {
                float e = 0.0f;
                if (!aKin && rbA.HasRigidBodyMode) e = std::max(e, rbA.Restitution);
                if (!bKin && rbB.HasRigidBodyMode) e = std::max(e, rbB.Restitution);

                float mAinv = aKin ? 0.0f : 1.0f / rbA.Mass;
                float mBinv = bKin ? 0.0f : 1.0f / rbB.Mass;

                // Build representative OBB for inertia / contact-arm computation
                OBB dummyA, dummyB;
                if (aIsBox)       dummyA = obbA;
                else if (aIsPyr)  { dummyA = BuildOBB(scene, (uint32_t)i); } // OBB approx for inertia frame
                else {
                    dummyA.center = posA;
                    dummyA.axes[0] = glm::normalize(glm::vec3(wmA[0]));
                    dummyA.axes[1] = glm::normalize(glm::vec3(wmA[1]));
                    dummyA.axes[2] = glm::normalize(glm::vec3(wmA[2]));
                    dummyA.half = (tA == ColliderType::Capsule)
                        ? glm::vec3(rA, hA * 0.5f, rA)
                        : glm::vec3(rA);
                }
                if (bIsBox)       dummyB = obbB;
                else if (bIsPyr)  { dummyB = BuildOBB(scene, (uint32_t)j); }
                else {
                    dummyB.center = posB;
                    dummyB.axes[0] = glm::normalize(glm::vec3(wmB[0]));
                    dummyB.axes[1] = glm::normalize(glm::vec3(wmB[1]));
                    dummyB.axes[2] = glm::normalize(glm::vec3(wmB[2]));
                    dummyB.half = (tB == ColliderType::Capsule)
                        ? glm::vec3(rB, hB * 0.5f, rB)
                        : glm::vec3(rB);
                }

                // Contact arm — shape-aware
                auto contactArm = [&](bool isKin, ColliderType ct, const OBB& obb,
                                      const glm::vec3* pyrV, const glm::vec3& pos,
                                      float rad, float h, const glm::vec3& capAxis,
                                      const glm::vec3& n) -> glm::vec3
                {
                    if (isKin) return glm::vec3(0);
                    if (ct == ColliderType::Box)
                        return OBBContactCentroid(obb, n) - obb.center;
                    if (ct == ColliderType::Pyramid && pyrV) {
                        // Centroid of pyramid vertices most extreme in direction n
                        float tol = 1e-3f * (obb.half.x + obb.half.y + obb.half.z);
                        float maxP = -FLT_MAX;
                        for (int k = 0; k < 5; k++)
                            maxP = std::max(maxP, glm::dot(pyrV[k], n));
                        glm::vec3 sum(0); int cnt = 0;
                        for (int k = 0; k < 5; k++) {
                            if (glm::dot(pyrV[k], n) >= maxP - tol) { sum += pyrV[k]; cnt++; }
                        }
                        glm::vec3 centroid = sum / (float)cnt;
                        // Pyramid CM at H/4 from base (not geometric center)
                        glm::vec3 pyrCM = obb.center - obb.axes[1] * (obb.half.y * 0.5f);
                        return centroid - pyrCM;
                    }
                    if (ct == ColliderType::Sphere)
                        return n * rad; // center → contact point (contact is in direction of n)
                    if (ct == ColliderType::Capsule) {
                        // Center → contact point on hemisphere surface
                        float halfH = std::max(h * 0.5f - rad, 0.0f);
                        glm::vec3 top = pos + capAxis * halfH;
                        glm::vec3 bot = pos - capAxis * halfH;
                        glm::vec3 bestPt = (glm::dot(bot, n) > glm::dot(top, n)) ? bot : top;
                        return (bestPt + n * rad) - pos;
                    }
                    return glm::vec3(0);
                };

                glm::vec3 armA = contactArm(aKin, tA, dummyA, aIsPyr ? pyrVertsA : nullptr, posA, rA, hA, capAxisA, -normal);
                glm::vec3 armB = contactArm(bKin, tB, dummyB, bIsPyr ? pyrVertsB : nullptr, posB, rB, hB, capAxisB,  normal);

                // Inverse inertia tensor — per-shape
                auto getIInv = [](bool isKin, bool hasRB, ColliderType ct, float mass,
                                  const glm::vec3& half, float rad, float h) -> glm::vec3
                {
                    if (isKin || !hasRB) return glm::vec3(0);
                    switch (ct) {
                        case ColliderType::Box:     return BoxInertiaDiagInv(mass, half);
                        case ColliderType::Pyramid: return PyramidInertiaDiagInv(mass, half);
                        case ColliderType::Sphere:  return SphereInertiaDiagInv(mass, rad);
                        case ColliderType::Capsule: return CapsuleInertiaDiagInv(mass, rad, h);
                        default: return glm::vec3(0);
                    }
                };

                glm::vec3 IinvA = getIInv(aKin, rbA.HasRigidBodyMode, tA, rbA.Mass, dummyA.half, rA, hA);
                glm::vec3 IinvB = getIInv(bKin, rbB.HasRigidBodyMode, tB, rbB.Mass, dummyB.half, rB, hB);

                // Velocidades angulares em rad/s
                glm::vec3 omegaA = (!aKin && rbA.HasRigidBodyMode) ? glm::radians(rbA.AngularVelocity) : glm::vec3(0);
                glm::vec3 omegaB = (!bKin && rbB.HasRigidBodyMode) ? glm::radians(rbB.AngularVelocity) : glm::vec3(0);

                glm::vec3 vA = aKin ? glm::vec3(0) : rbA.Velocity;
                glm::vec3 vB = bKin ? glm::vec3(0) : rbB.Velocity;

                // Velocidade relativa no ponto de contato (inclui componente angular)
                glm::vec3 vA_c = vA + glm::cross(omegaA, armA);
                glm::vec3 vB_c = vB + glm::cross(omegaB, armB);
                float vRel = glm::dot(vA_c - vB_c, normal);

                if (-vRel < 1.0f) e = 0.0f;

                if (vRel < 0.0f && (mAinv + mBinv) > 0.0f)
                {
                    // Denominador inclui termos angulares: dot(n, (I^-1*(r×n))×r)
                    float angTermA = (!aKin && rbA.HasRigidBodyMode) ?
                        glm::dot(glm::cross(ApplyIInvWorld(dummyA, IinvA, glm::cross(armA, normal)), armA), normal) : 0.0f;
                    float angTermB = (!bKin && rbB.HasRigidBodyMode) ?
                        glm::dot(glm::cross(ApplyIInvWorld(dummyB, IinvB, glm::cross(armB, normal)), armB), normal) : 0.0f;

                    float denom = mAinv + mBinv + angTermA + angTermB;

                    if (denom > 1e-6f)
                    {
                        float jSc = -(1.0f + e) * vRel / denom;
                        glm::vec3 impulse = jSc * normal;

                        if (!aKin) rbA.Velocity += impulse * mAinv;
                        if (!bKin) rbB.Velocity -= impulse * mBinv;

                        // Impulso angular: delta_omega = I^-1 * (r × impulso)
                        if (!aKin && rbA.HasRigidBodyMode)
                        {
                            rbA.AngularVelocity += glm::degrees(ApplyIInvWorld(dummyA, IinvA, glm::cross(armA, impulse)));
                            float spd = glm::length(rbA.AngularVelocity);
                            if (spd > 720.0f) rbA.AngularVelocity *= 720.0f / spd;
                        }
                        if (!bKin && rbB.HasRigidBodyMode)
                        {
                            rbB.AngularVelocity += glm::degrees(ApplyIInvWorld(dummyB, IinvB, glm::cross(armB, -impulse)));
                            float spd = glm::length(rbB.AngularVelocity);
                            if (spd > 720.0f) rbB.AngularVelocity *= 720.0f / spd;
                        }

                        // --- Friction (Coulomb model) ---
                        // Recompute relative velocity after normal impulse
                        glm::vec3 omegaA2 = (!aKin && rbA.HasRigidBodyMode) ? glm::radians(rbA.AngularVelocity) : glm::vec3(0);
                        glm::vec3 omegaB2 = (!bKin && rbB.HasRigidBodyMode) ? glm::radians(rbB.AngularVelocity) : glm::vec3(0);
                        glm::vec3 vRel2   = (aKin ? glm::vec3(0) : rbA.Velocity + glm::cross(omegaA2, armA))
                                          - (bKin ? glm::vec3(0) : rbB.Velocity + glm::cross(omegaB2, armB));

                        glm::vec3 vTang    = vRel2 - glm::dot(vRel2, normal) * normal;
                        float     vTangLen = glm::length(vTang);

                        float fricCoef = 0.0f;
                        if (!aKin && rbA.HasRigidBodyMode) fricCoef = std::max(fricCoef, rbA.FrictionCoef);
                        if (!bKin && rbB.HasRigidBodyMode) fricCoef = std::max(fricCoef, rbB.FrictionCoef);

                        if (vTangLen > 1e-4f && fricCoef > 0.0f)
                        {
                            glm::vec3 tangent = vTang / vTangLen;

                            float tAngTermA = (!aKin && rbA.HasRigidBodyMode) ?
                                glm::dot(glm::cross(ApplyIInvWorld(dummyA, IinvA, glm::cross(armA, tangent)), armA), tangent) : 0.0f;
                            float tAngTermB = (!bKin && rbB.HasRigidBodyMode) ?
                                glm::dot(glm::cross(ApplyIInvWorld(dummyB, IinvB, glm::cross(armB, tangent)), armB), tangent) : 0.0f;

                            float tDenom = mAinv + mBinv + tAngTermA + tAngTermB;
                            if (tDenom > 1e-6f)
                            {
                                float jFric = glm::clamp(-vTangLen / tDenom,
                                                         -fricCoef * jSc,
                                                          fricCoef * jSc);
                                glm::vec3 fricImpulse = jFric * tangent;

                                if (!aKin) rbA.Velocity += fricImpulse * mAinv;
                                if (!bKin) rbB.Velocity -= fricImpulse * mBinv;

                                if (!aKin && rbA.HasRigidBodyMode)
                                {
                                    rbA.AngularVelocity += glm::degrees(ApplyIInvWorld(dummyA, IinvA, glm::cross(armA, fricImpulse)));
                                    float spd = glm::length(rbA.AngularVelocity);
                                    if (spd > 720.0f) rbA.AngularVelocity *= 720.0f / spd;
                                }
                                if (!bKin && rbB.HasRigidBodyMode)
                                {
                                    rbB.AngularVelocity += glm::degrees(ApplyIInvWorld(dummyB, IinvB, glm::cross(armB, -fricImpulse)));
                                    float spd = glm::length(rbB.AngularVelocity);
                                    if (spd > 720.0f) rbB.AngularVelocity *= 720.0f / spd;
                                }
                            }
                        }
                    }
                }

                if (!aKin && normal.y >  0.5f) rbA.IsGrounded = true;
                if (!bKin && normal.y < -0.5f) rbB.IsGrounded = true;
            }
            else
            {
                auto resolveVelocity = [&](RigidBodyComponent& rb, float sign)
                {
                    float vAlong = glm::dot(rb.Velocity, normal) * sign;
                    if (vAlong < 0.0f)
                        rb.Velocity -= normal * sign * vAlong;
                    if (normal.y * sign > 0.5f)
                        rb.IsGrounded = true;
                };
                if (!aKin) resolveVelocity(rbA,  1.0f);
                if (!bKin) resolveVelocity(rbB, -1.0f);
            }
        }
    }
}

} // namespace tsu
