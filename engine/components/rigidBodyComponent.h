#pragma once
#include <glm/glm.hpp>

namespace tsu {

// Available collider types
enum class ColliderType {
    None,
    Box,       // AABB — for cubes
    Sphere,    // for spheres
    Capsule,   // for cylinders/capsules
    Pyramid    // approximated as cone AABB
};

struct RigidBodyComponent
{
    // --- Modules (each can be added/removed independently) ---
    bool HasGravityModule  = false;
    bool HasColliderModule = false;

    // --- Gravity module fields ---
    bool  UseGravity   = true;
    bool  IsKinematic  = false;
    float Mass         = 1.0f;
    float Drag         = 0.01f;

    // --- Runtime velocity (managed by physics system) ---
    glm::vec3 Velocity  = { 0.0f, 0.0f, 0.0f };
    bool      IsGrounded = false;

    // --- Collider module fields ---
    ColliderType Collider = ColliderType::Box;  // shape when module is active
    glm::vec3 ColliderSize   = { 1.0f, 1.0f, 1.0f };
    float     ColliderRadius = 0.5f;
    float     ColliderHeight = 1.0f;
    glm::vec3 ColliderOffset = { 0.0f, 0.0f, 0.0f };
};

} // namespace tsu