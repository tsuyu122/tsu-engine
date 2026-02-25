#pragma once
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

#include "components/transformComponent.h"
#include "components/cameraComponent.h"
#include "components/meshRendererComponent.h"

namespace tsu {

// ================================================================
// RigidBodyComponent
// ================================================================

enum class ColliderType { None, Box, Sphere, Capsule, Pyramid };

struct RigidBodyComponent
{
    // --- Module flags ---
    bool          HasGravityModule  = false;
    bool          HasColliderModule = false;

    // --- Gravity module ---
    bool          UseGravity      = true;
    bool          IsKinematic     = false;
    float         Mass            = 1.0f;
    float         Drag            = 0.01f;
    glm::vec3     Velocity        = { 0.0f, 0.0f, 0.0f };
    bool          IsGrounded      = false;

    // --- Collider module ---
    ColliderType  Collider        = ColliderType::Box;
    glm::vec3     ColliderSize    = { 1.0f, 1.0f, 1.0f };
    float         ColliderRadius  = 0.5f;
    float         ColliderHeight  = 1.0f;
    glm::vec3     ColliderOffset  = { 0.0f, 0.0f, 0.0f };
};

// ================================================================
// GameCameraComponent
// ================================================================

struct GameCameraComponent
{
    float     FOV   = 75.0f;
    float     Near  = 0.1f;
    float     Far   = 500.0f;
    bool      Active = false;

    glm::vec3 Front   = { 0.0f, 0.0f, -1.0f };
    glm::vec3 Up      = { 0.0f, 1.0f,  0.0f };
    glm::vec3 Right   = { 1.0f, 0.0f,  0.0f };
    glm::vec3 WorldUp = { 0.0f, 1.0f,  0.0f };

    float Yaw   = -90.0f;
    float Pitch =   0.0f;

    glm::mat4 GetProjection(float aspect) const
    {
        return glm::perspective(glm::radians(FOV), aspect, Near, Far);
    }

    void UpdateVectors()
    {
        glm::vec3 front;
        front.x = cos(glm::radians(Yaw))  * cos(glm::radians(Pitch));
        front.y = sin(glm::radians(Pitch));
        front.z = sin(glm::radians(Yaw))  * cos(glm::radians(Pitch));
        Front = glm::normalize(front);
        Right = glm::normalize(glm::cross(Front, WorldUp));
        Up    = glm::normalize(glm::cross(Right, Front));
    }
};

// ================================================================
// SceneGroup  --  folder / group in the hierarchy
// ================================================================

struct SceneGroup
{
    std::string        Name        = "Group";
    int                ParentGroup = -1;          // -1 = scene root
    TransformComponent Transform;                 // local (relative to parent group)
    std::vector<int>   ChildEntities;             // entity indices
    std::vector<int>   ChildGroups;               // group  indices
    bool               UICollapsed = false;

    // Optional components on the group itself
    RigidBodyComponent  RigidBody;
    GameCameraComponent GameCamera;
};

// ================================================================
// Scene
// ================================================================

class Entity;

class Scene
{
public:
    // --- Per-entity parallel arrays ---
    std::vector<TransformComponent>    Transforms;     // local (relative to group/parent)
    std::vector<CameraComponent>       Cameras;
    std::vector<MeshRendererComponent> MeshRenderers;
    std::vector<RigidBodyComponent>    RigidBodies;
    std::vector<GameCameraComponent>   GameCameras;
    std::vector<std::string>           EntityNames;
    std::vector<int>                   EntityGroups;   // group index, -1 = no group
    std::vector<int>                   EntityParents;  // entity parent index, -1 = none
    std::vector<std::vector<int>>      EntityChildren; // entity-children per entity

    // --- Hierarchy display order ---
    std::vector<int>                   RootOrder;      // root-level entity display order

    // --- Groups (folders) ---
    std::vector<SceneGroup>            Groups;

    // --- Entity API ---
    Entity CreateEntity(const std::string& name = "Entity");
    void   OnUpdate(float dt);
    int    GetActiveGameCamera() const;

    // Move entity to be a child of another entity (-1 = unparent)
    void   SetEntityParent(int childIdx, int parentIdx);
    // Move entity into a group (converts world position to local)
    void   SetEntityGroup(int entityIdx, int newGroup);
    // Remove entity from any group (converts to world position)
    void   UnparentEntity(int entityIdx);

    // Create a new empty group
    int    CreateGroup(const std::string& name = "Group", int parentGroup = -1);
    // Delete a group (unparents all its children first)
    void   DeleteGroup(int groupIdx);

    // --- World-space helpers ---
    glm::mat4 GetEntityWorldMatrix(int entityIdx) const;
    glm::mat4 GetGroupWorldMatrix (int groupIdx)  const;
    glm::vec3 GetEntityWorldPos   (int entityIdx) const;
};

} // namespace tsu