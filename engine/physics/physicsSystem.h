#pragma once
#include "scene/scene.h"
#include <array>

namespace tsu {

// Oriented Bounding Box — suporta rotação arbitrária
struct OBB
{
    glm::vec3 center;      // centro em world-space
    glm::vec3 axes[3];     // eixos locais X/Y/Z em world-space (vetores unitários)
    glm::vec3 half;        // semi-extensões ao longo de cada eixo
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

    // Capsule vs OBB (aproxima cápsula como esfera no ponto mais próximo)
    static bool CheckCapsuleOBB(
        const glm::vec3& capPos, float radius, float height,
        const glm::vec3& capAxisWorld,   // eixo do capsule em world-space (normalizado)
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

    // Pyramid vs OBB — SAT com face-normals da pirâmide + eixos cruzados
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

    // Pyramid vs Pyramid — SAT entre duas pirâmides
    static bool CheckPyramidPyramid(
        const glm::vec3 vertsA[5], const glm::vec3 vertsB[5],
        glm::vec3& outNormal, float& outDepth);

    // Constrói um OBB a partir dos dados de um entity + collider
    static OBB BuildOBB(const Scene& scene, uint32_t id);

    // Constrói os 5 vértices world-space de uma pirâmide
    // v0..v3 = base, v4 = apex
    static void BuildPyramidVerts(const Scene& scene, uint32_t id, glm::vec3 outVerts[5]);
};

} // namespace tsu