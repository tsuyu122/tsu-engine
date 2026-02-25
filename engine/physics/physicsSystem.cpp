#include "physics/physicsSystem.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace tsu {

// ----------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------

glm::vec3 PhysicsSystem::GetColliderWorldPos(const Scene& scene, uint32_t id)
{
    // Use world position (handles group parents) + collider offset
    return scene.GetEntityWorldPos((int)id) + scene.RigidBodies[id].ColliderOffset;
}

glm::vec3 PhysicsSystem::GetColliderWorldSize(const Scene& scene, uint32_t id)
{
    const auto& rb = scene.RigidBodies[id];
    const auto& t  = scene.Transforms[id];
    return rb.ColliderSize * t.Scale;
}

// ----------------------------------------------------------------
// Detecção Box vs Box (AABB)
// ----------------------------------------------------------------

bool PhysicsSystem::CheckBoxBox(
    const glm::vec3& posA, const glm::vec3& sizeA,
    const glm::vec3& posB, const glm::vec3& sizeB,
    glm::vec3& outNormal, float& outDepth)
{
    glm::vec3 halfA = sizeA * 0.5f;
    glm::vec3 halfB = sizeB * 0.5f;

    glm::vec3 diff = posB - posA;
    glm::vec3 overlap;
    overlap.x = (halfA.x + halfB.x) - std::abs(diff.x);
    overlap.y = (halfA.y + halfB.y) - std::abs(diff.y);
    overlap.z = (halfA.z + halfB.z) - std::abs(diff.z);

    if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0)
        return false;

    // Eixo de menor penetração — normal aponta de B para A
    if (overlap.x < overlap.y && overlap.x < overlap.z)
    {
        outDepth  = overlap.x;
        outNormal = glm::vec3(diff.x < 0 ? 1.0f : -1.0f, 0, 0);
    }
    else if (overlap.y < overlap.z)
    {
        outDepth  = overlap.y;
        outNormal = glm::vec3(0, diff.y < 0 ? 1.0f : -1.0f, 0);
    }
    else
    {
        outDepth  = overlap.z;
        outNormal = glm::vec3(0, 0, diff.z < 0 ? 1.0f : -1.0f);
    }
    return true;
}

// ----------------------------------------------------------------
// Sphere vs Pyramid  (proper 5-plane test in local pyramid space)
// Pyramid: apex at (0, 0.5, 0), base at y=-0.5, half-base = 0.5
// ColliderSize is applied as a scale
// ----------------------------------------------------------------

bool PhysicsSystem::CheckSpherePyramid(
    const glm::vec3& sphereWorldPos, float radius,
    const glm::vec3& pyrWorldPos,    const glm::vec3& pyrScale,
    glm::vec3& outNormal, float& outDepth)
{
    // Transform sphere center into un-scaled pyramid local space
    glm::vec3 local = (sphereWorldPos - pyrWorldPos) / pyrScale;

    // Pyramid spans y: [-0.5, 0.5], at height y the half-side = (0.5 - y) * 0.5
    // Quick reject
    if (local.y > 0.5f + radius / pyrScale.y) return false;
    if (local.y < -0.5f - radius / pyrScale.y) return false;

    // 5 planes in local space (normal, d where plane: dot(n,p) >= d)
    struct Plane { glm::vec3 n; float d; };
    // Precomputed normals for unit pyramid (apex=0,0.5,0 base corners at +-0.5,-0.5,+-0.5)
    static const Plane planes[5] = {
        { glm::normalize(glm::vec3( 0,  0.5f,  1.0f)),  0.0f },  // front
        { glm::normalize(glm::vec3( 0,  0.5f, -1.0f)),  0.0f },  // back
        { glm::normalize(glm::vec3( 1.0f, 0.5f, 0)),    0.0f },  // right
        { glm::normalize(glm::vec3(-1.0f, 0.5f, 0)),    0.0f },  // left
        { glm::vec3(0, -1, 0),                          0.5f  },  // bottom
    };

    float minPen = 1e30f;
    glm::vec3 bestN(0,1,0);

    for (auto& pl : planes)
    {
        // Effective radius in plane normal direction (accounting for scale)
        glm::vec3 nWorld = glm::normalize(pl.n / pyrScale);
        float     effR   = radius / glm::length(pl.n / pyrScale);
        float     dist   = glm::dot(pl.n, local) - pl.d;
        float     pen    = effR - dist;
        if (pen <= 0.0f) return false;  // outside this plane -> no collision
        if (pen < minPen) { minPen = pen; bestN = nWorld; }
    }

    outDepth  = minPen;
    // Normal points from pyramid to sphere
    outNormal = glm::dot(bestN, sphereWorldPos - pyrWorldPos) >= 0
                ? bestN : -bestN;
    return true;
}

// ----------------------------------------------------------------
// Box vs Pyramid — approximate as AABB but shrink based on height
// ----------------------------------------------------------------

bool PhysicsSystem::CheckBoxPyramid(
    const glm::vec3& boxPos,  const glm::vec3& boxSize,
    const glm::vec3& pyrPos,  const glm::vec3& pyrScale,
    glm::vec3& outNormal, float& outDepth)
{
    // Use the box center to approximate with sphere-pyramid for the center,
    // then fall back to a tight AABB if that fails
    float r = glm::length(boxSize) * 0.5f;
    if (CheckSpherePyramid(boxPos, r, pyrPos, pyrScale, outNormal, outDepth))
        return true;
    // Fallback: AABB
    return CheckBoxBox(boxPos, boxSize, pyrPos, pyrScale, outNormal, outDepth);
}

// ----------------------------------------------------------------
// Sphere vs Sphere
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
    // Normal aponta de B para A
    outNormal = dist > 0.0001f ? glm::normalize(posA - posB) : glm::vec3(0, 1, 0);
    return true;
}

// ----------------------------------------------------------------
// Sphere vs Box
// ----------------------------------------------------------------

bool PhysicsSystem::CheckSphereBox(
    const glm::vec3& spherePos, float radius,
    const glm::vec3& boxPos,    const glm::vec3& boxSize,
    glm::vec3& outNormal, float& outDepth)
{
    glm::vec3 half  = boxSize * 0.5f;
    glm::vec3 local = spherePos - boxPos;

    // Ponto mais próximo no box à esfera
    glm::vec3 closest;
    closest.x = std::max(-half.x, std::min(local.x, half.x));
    closest.y = std::max(-half.y, std::min(local.y, half.y));
    closest.z = std::max(-half.z, std::min(local.z, half.z));

    glm::vec3 delta = local - closest;
    float dist = glm::length(delta);

    if (dist >= radius) return false;

    outDepth  = radius - dist;
    outNormal = dist > 0.0001f ? glm::normalize(delta) : glm::vec3(0, 1, 0);
    return true;
}

// ----------------------------------------------------------------
// Capsule vs Box — aproxima cápsula como uma esfera no ponto mais
// próximo do eixo vertical ao box
// ----------------------------------------------------------------

bool PhysicsSystem::CheckCapsuleBox(
    const glm::vec3& capPos, float radius, float height,
    const glm::vec3& boxPos, const glm::vec3& boxSize,
    glm::vec3& outNormal, float& outDepth)
{
    float halfH = height * 0.5f - radius;
    halfH = std::max(halfH, 0.0f);

    // Ponto no eixo da cápsula mais próximo do centro do box
    glm::vec3 boxLocal = boxPos - capPos;
    float t = std::max(-halfH, std::min(boxLocal.y, halfH));
    glm::vec3 closest = capPos + glm::vec3(0, t, 0);

    return CheckSphereBox(closest, radius, boxPos, boxSize, outNormal, outDepth);
}

// ----------------------------------------------------------------
// Update principal
// ----------------------------------------------------------------

void PhysicsSystem::Update(Scene& scene, float dt)
{
    const float MAX_FALL_SPEED = -50.0f;

    // --- Gravity + integration ---
    for (size_t i = 0; i < scene.RigidBodies.size(); i++)
    {
        auto& rb = scene.RigidBodies[i];
        if (!rb.HasGravityModule) continue;
        if (rb.IsKinematic) continue;

        if (rb.UseGravity)
        {
            rb.Velocity.y += Gravity * dt;
            rb.Velocity.y  = std::max(rb.Velocity.y, MAX_FALL_SPEED);
        }

        rb.Velocity.x *= (1.0f - rb.Drag);
        rb.Velocity.z *= (1.0f - rb.Drag);

        scene.Transforms[i].Position += rb.Velocity * dt;

        rb.IsGrounded = false;
    }

    // --- Collision resolution (3 iterations for stability) ---
    for (int iter = 0; iter < 3; ++iter)
        ResolveCollisions(scene);
}

// ----------------------------------------------------------------
// Resolução de colisões
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

            // Two kinematics do not interact
            if (rbA.IsKinematic && rbB.IsKinematic) continue;

            bool aKin = rbA.IsKinematic;
            bool bKin = rbB.IsKinematic;

            glm::vec3 posA = GetColliderWorldPos(scene, (uint32_t)i);
            glm::vec3 posB = GetColliderWorldPos(scene, (uint32_t)j);

            glm::vec3 normal(0);
            float depth = 0;
            bool hit = false;

            // Despacho de colisão por tipo
            ColliderType tA = rbA.Collider;
            ColliderType tB = rbB.Collider;

            if (tA == ColliderType::Box)
            {
                glm::vec3 sA = GetColliderWorldSize(scene, (uint32_t)i);
                if (tB == ColliderType::Box)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckBoxBox(posA, sA, posB, sB, normal, depth);
                }
                else if (tB == ColliderType::Pyramid)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckBoxPyramid(posA, sA, posB, sB, normal, depth);
                }
                else if (tB == ColliderType::Sphere)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSphereBox(posB, rB, posA, sA, normal, depth);
                    normal = -normal;
                }
                else if (tB == ColliderType::Capsule)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    float hB = rbB.ColliderHeight * scene.Transforms[j].Scale.y;
                    hit = CheckCapsuleBox(posB, rB, hB, posA, sA, normal, depth);
                    normal = -normal;
                }
            }
            else if (tA == ColliderType::Pyramid)
            {
                glm::vec3 sA = GetColliderWorldSize(scene, (uint32_t)i);
                if (tB == ColliderType::Box)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckBoxPyramid(posB, sB, posA, sA, normal, depth);
                    normal = -normal;
                }
                else if (tB == ColliderType::Pyramid)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckBoxBox(posA, sA, posB, sB, normal, depth); // approx
                }
                else if (tB == ColliderType::Sphere)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSpherePyramid(posB, rB, posA, sA, normal, depth);
                    normal = -normal;
                }
                else if (tB == ColliderType::Capsule)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSpherePyramid(posB, rB, posA, sA, normal, depth);
                    normal = -normal;
                }
            }
            else if (tA == ColliderType::Sphere)
            {
                float rA = rbA.ColliderRadius * scene.Transforms[i].Scale.x;
                if (tB == ColliderType::Sphere)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSphereSphere(posA, rA, posB, rB, normal, depth);
                }
                else if (tB == ColliderType::Box)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckSphereBox(posA, rA, posB, sB, normal, depth);
                }
                else if (tB == ColliderType::Pyramid)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckSpherePyramid(posA, rA, posB, sB, normal, depth);
                }
                else if (tB == ColliderType::Capsule)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSphereSphere(posA, rA, posB, rB, normal, depth);
                }
            }
            else if (tA == ColliderType::Capsule)
            {
                float rA = rbA.ColliderRadius * scene.Transforms[i].Scale.x;
                float hA = rbA.ColliderHeight * scene.Transforms[i].Scale.y;
                if (tB == ColliderType::Box)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckCapsuleBox(posA, rA, hA, posB, sB, normal, depth);
                }
                else if (tB == ColliderType::Pyramid)
                {
                    glm::vec3 sB = GetColliderWorldSize(scene, (uint32_t)j);
                    hit = CheckSpherePyramid(posA, rA, posB, sB, normal, depth); // approx
                }
                else if (tB == ColliderType::Sphere)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSphereSphere(posA, rA, posB, rB, normal, depth);
                }
                else if (tB == ColliderType::Capsule)
                {
                    float rB = rbB.ColliderRadius * scene.Transforms[j].Scale.x;
                    hit = CheckSphereSphere(posA, rA, posB, rB, normal, depth);
                }
            }

            if (!hit) continue;

            // Separar entidades — normal aponta de B (j) para A (i)
            if (!aKin && !bKin)
            {
                scene.Transforms[i].Position += normal * (depth * 0.5f);
                scene.Transforms[j].Position -= normal * (depth * 0.5f);
            }
            else if (!aKin)
            {
                scene.Transforms[i].Position += normal * depth;
            }
            else
            {
                scene.Transforms[j].Position -= normal * depth;
            }

            // Resolver velocidades — A para em -normal, B para em +normal
            auto resolveVelocity = [&](RigidBodyComponent& rbDyn, float sign)
            {
                float vAlong = glm::dot(rbDyn.Velocity, normal) * sign;
                if (vAlong < 0.0f)
                {
                    const float bounce = 0.1f;
                    rbDyn.Velocity -= normal * sign * vAlong * (1.0f + bounce);
                }
                // Marca grounded se a normal empurra pra cima
                if (normal.y * sign > 0.5f)
                    rbDyn.IsGrounded = true;
            };

            if (!aKin) resolveVelocity(rbA,  1.0f);
            if (!bKin) resolveVelocity(rbB, -1.0f);
        }
    }
}

} // namespace tsu