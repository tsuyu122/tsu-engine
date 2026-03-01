#pragma once
#include "scene/scene.h"
#include <array>

namespace tsu {

// Oriented Bounding Box — supports arbitrary rotation
struct OBB
{
    glm::vec3 center;      // center in world-space
    glm::vec3 axes[3];     // local X/Y/Z axes in world-space (unit vectors)
    glm::vec3 half;        // half-extents along each axis
};

class PhysicsSystem
{
public:
    static constexpr float Gravity = -9.8f;
    static void Update(Scene& scene, float dt);

private:
    static void ResolveCollisions(Scene& scene);

    // OBB vs OBB — SAT com 15 eixos; normal aponta de B para A
    static bool CheckOBBOBB(
        const OBB& A, const OBB& B,
        glm::vec3& outNormal, float& outDepth);

    // Sphere vs OBB
    static bool CheckSphereOBB(
        const glm::vec3& spherePos, float radius,
        const OBB& box,
        glm::vec3& outNormal, float& outDepth);

    // Sphere vs Sphere
    static bool CheckSphereSphere(
        const glm::vec3& posA, float rA,
        const glm::vec3& posB, float rB,
        glm::vec3& outNormal, float& outDepth);

    // Capsule vs OBB (approximates capsule as sphere at closest point)
    static bool CheckCapsuleOBB(
        const glm::vec3& capPos, float radius, float height,
        const glm::vec3& capAxisWorld,   // capsule axis in world-space (normalized)
        const OBB& box,
        glm::vec3& outNormal, float& outDepth);

    // Capsule vs Sphere (closest point on segment → sphere check)
    static bool CheckCapsuleSphere(
        const glm::vec3& capPos, float capRadius, float capHeight,
        const glm::vec3& capAxisWorld,
        const glm::vec3& spherePos, float sphereRadius,
        glm::vec3& outNormal, float& outDepth);

    // Capsule vs Capsule (closest points between segments → sphere check)
    static bool CheckCapsuleCapsule(
        const glm::vec3& capPosA, float rA, float hA, const glm::vec3& axisA,
        const glm::vec3& capPosB, float rB, float hB, const glm::vec3& axisB,
        glm::vec3& outNormal, float& outDepth);

    // Pyramid vs OBB — SAT with pyramid face-normals + cross-product axes
    static bool CheckPyramidOBB(
        const glm::vec3 pyrVerts[5],
        const OBB& box,
        glm::vec3& outNormal, float& outDepth);

    // Sphere vs Pyramid — closest point on pyramid faces
    static bool CheckSpherePyramid(
        const glm::vec3& spherePos, float radius,
        const glm::vec3 pyrVerts[5],
        glm::vec3& outNormal, float& outDepth);

    // Capsule vs Pyramid — closest on segment → sphere-pyramid
    static bool CheckCapsulePyramid(
        const glm::vec3& capPos, float radius, float height,
        const glm::vec3& capAxisWorld,
        const glm::vec3 pyrVerts[5],
        glm::vec3& outNormal, float& outDepth);

    // Pyramid vs Pyramid — SAT between two pyramids
    static bool CheckPyramidPyramid(
        const glm::vec3 vertsA[5], const glm::vec3 vertsB[5],
        glm::vec3& outNormal, float& outDepth);

    // Builds an OBB from entity + collider data
    static OBB BuildOBB(const Scene& scene, uint32_t id);

    // Builds the 5 world-space vertices of a pyramid
    // v0..v3 = base, v4 = apex
    static void BuildPyramidVerts(const Scene& scene, uint32_t id, glm::vec3 outVerts[5]);
};

} // namespace tsu