#pragma once
#include "scene/scene.h"

namespace tsu {

class PhysicsSystem
{
public:
    static constexpr float Gravity = -9.8f;
    static void Update(Scene& scene, float dt);

private:
    static void ResolveCollisions(Scene& scene);

    static bool CheckBoxBox(
        const glm::vec3& posA, const glm::vec3& sizeA,
        const glm::vec3& posB, const glm::vec3& sizeB,
        glm::vec3& outNormal, float& outDepth);

    static bool CheckSphereSphere(
        const glm::vec3& posA, float rA,
        const glm::vec3& posB, float rB,
        glm::vec3& outNormal, float& outDepth);

    static bool CheckSphereBox(
        const glm::vec3& spherePos, float radius,
        const glm::vec3& boxPos,    const glm::vec3& boxSize,
        glm::vec3& outNormal, float& outDepth);

    static bool CheckCapsuleBox(
        const glm::vec3& capPos, float radius, float height,
        const glm::vec3& boxPos, const glm::vec3& boxSize,
        glm::vec3& outNormal, float& outDepth);

    static bool CheckSpherePyramid(
        const glm::vec3& spherePos, float radius,
        const glm::vec3& pyrPos,    const glm::vec3& pyrScale,
        glm::vec3& outNormal, float& outDepth);

    static bool CheckBoxPyramid(
        const glm::vec3& boxPos,  const glm::vec3& boxSize,
        const glm::vec3& pyrPos,  const glm::vec3& pyrScale,
        glm::vec3& outNormal, float& outDepth);

    static glm::vec3 GetColliderWorldPos (const Scene& scene, uint32_t id);
    static glm::vec3 GetColliderWorldSize(const Scene& scene, uint32_t id);
};

} // namespace tsu