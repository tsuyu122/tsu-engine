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

    // --- Per-module enabled flags (toggled via inspector toggle button) ---
    bool          GravityEnabled       = true;
    bool          ColliderEnabled      = true;
    bool          RigidBodyModeEnabled = true;

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
    bool          ShowCollider    = false;   // draws green wireframe in editor

    // --- Fall speed multiplier (applied to gravity acceleration) ---
    float         FallSpeedMultiplier = 1.0f;

    // --- RigidBody full simulation mode ---
    // Requires HasGravityModule for gravity. Adds rotation from impacts.
    bool          HasRigidBodyMode = false;
    glm::vec3     AngularVelocity  = { 0.0f, 0.0f, 0.0f }; // degrees / second
    float         AngularDamping   = 0.0f;    // 0 = no damping, 1 = instant stop
    float         Restitution      = 0.25f;   // bounce coefficient 0=no bounce 1=perfect
    float         FrictionCoef     = 0.15f;   // lateral friction on impact
};

// ================================================================
// GameCameraComponent
// ================================================================

struct GameCameraComponent
{
    float     FOV   = 75.0f;
    float     Near  = 0.1f;
    float     Far   = 500.0f;
    bool      Active  = false;
    bool      Enabled = true;  // inspector enable/disable toggle

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
// Material asset (stored in the scene asset list)
// ================================================================

struct MaterialAsset
{
    std::string Name = "Material";
    int         Folder = -1; // index into scene-level folders, -1 = root
    glm::vec3   Color = {1.0f, 1.0f, 1.0f};
    std::string TexturePath; // optional, resolver/loader not implemented yet
    unsigned int TextureID = 0; // GL texture handle if loaded
};

enum class ChannelVariableType { Boolean, Float, String };

struct ChannelVariable
{
    std::string         Name        = "Channel";
    ChannelVariableType Type        = ChannelVariableType::Boolean;
    bool                BoolValue   = false;
    float               FloatValue  = 0.0f;
    std::string         StringValue = "";
};

enum class PlayerInputMode { Local, Channels };

// ================================================================
// PlayerMovementMode -- how the controller translates input into world movement
//
//  Mode 1 (WorldWithCollision)  : axes always point along WORLD X/Z (ignores player
//                                  rotation).  Physics system resolves collisions.
//  Mode 2 (LocalWithCollision)  : axes are relative to the entity's FACING DIRECTION.
//                                  "Forward" always moves where the player is looking.
//                                  Physics system resolves collisions.  (DEFAULT)
//  Mode 3 (WorldNoCollision)    : same as mode 1 but skips physics collision (noclip).
//  Mode 4 (LocalNoCollision)    : same as mode 2 but skips physics collision (noclip).
// ================================================================
enum class PlayerMovementMode {
    WorldWithCollision = 1,   // mode 1: global-space, physics collision
    LocalWithCollision = 2,   // mode 2: local-space,  physics collision  (default)
    WorldNoCollision   = 3,   // mode 3: global-space, noclip
    LocalNoCollision   = 4,   // mode 4: local-space,  noclip
};

struct PlayerControllerComponent
{
    bool            Active  = false;
    bool            Enabled = true;  // inspector enable/disable toggle
    PlayerInputMode InputMode    = PlayerInputMode::Local;
    PlayerMovementMode MovementMode = PlayerMovementMode::LocalWithCollision;

    float WalkSpeed        = 4.0f;
    float RunMultiplier    = 1.8f;
    float CrouchMultiplier = 0.45f;

    // Local mode key bindings (GLFW key codes)
    // Engine forward = +X, left = +Z
    int KeyForward = 87;   // W
    int KeyBack    = 83;   // S
    int KeyLeft    = 65;   // A
    int KeyRight   = 68;   // D
    int KeyRun     = 340;  // Left Shift
    int KeyCrouch  = 341;  // Left Control

    // Channels mode bindings
    int ChForward = 0;
    int ChBack    = 1;
    int ChLeft    = 2;
    int ChRight   = 3;
    int ChRun     = 4;
    int ChCrouch  = 5;

    // External systems may block actions by toggling these flags
    bool AllowRun    = true;
    bool AllowCrouch = true;

    // Runtime state (readable by external systems)
    bool      IsRunning    = false;
    bool      IsCrouching  = false;
    glm::vec3 LastMoveAxis = {0.0f, 0.0f, 0.0f};
};

// ================================================================
// MouseLookComponent – controls entity rotation via mouse in Game mode
// ================================================================

struct MouseLookComponent
{
    bool  Active  = false;
    bool  Enabled = true;

    // Yaw target (Y-axis): typically the player body / parent object
    int   YawTargetEntity   = -1;
    // Pitch target (Z-axis): typically the camera / child object
    // Rotation.z = pitch for an X-forward entity (tilts nose up/down in XY plane)
    int   PitchTargetEntity = -1;

    float SensitivityX = 0.15f;   // horizontal sensitivity
    float SensitivityY = 0.15f;   // vertical sensitivity
    bool  InvertX      = false;
    bool  InvertY      = false;

    bool  ClampPitch   = true;
    float PitchMin     = -89.0f;
    float PitchMax     =  89.0f;

    // Runtime accumulated angles (reset when component is activated)
    float CurrentPitch = 0.0f;
    float CurrentYaw   = 0.0f;
};

// ================================================================
// LightComponent  --  scene lighting: Directional, Point, Spot, Area
// ================================================================

enum class LightType { Directional = 0, Point = 1, Spot = 2, Area = 3 };

struct LightComponent
{
    bool      Active      = false;
    bool      Enabled     = true;
    LightType Type        = LightType::Directional;
    glm::vec3 Color       = { 1.0f, 1.0f, 1.0f };
    float     Temperature = 6500.0f;  // Kelvin: 1000-12000; 2700=warm, 6500=daylight
    float     Intensity   = 1.0f;
    float     Range       = 10.0f;    // Point / Spot / Area attenuation distance
    float     InnerAngle  = 30.0f;    // Spot inner cone angle (degrees)
    float     OuterAngle  = 45.0f;    // Spot outer cone angle (degrees)
    float     Width       = 1.0f;     // Area light width
    float     Height      = 1.0f;     // Area light height
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
    std::vector<int>                   EntityMaterial; // per-entity material index (-1 = none)
    std::vector<glm::vec3>             EntityColors;   // per-entity color override (used when no material)
    std::vector<PlayerControllerComponent> PlayerControllers;
    std::vector<MouseLookComponent>         MouseLooks;
    std::vector<LightComponent>             Lights;

    // --- Hierarchy display order ---
    std::vector<int>                   RootOrder;      // root-level entity display order

    // --- Groups (folders) ---
    std::vector<SceneGroup>            Groups;
    // --- Assets ---
    std::vector<std::string>           Folders;        // flat list of folder names
    std::vector<int>                   FolderParents;  // parent folder index per folder (-1 = root)
    std::vector<MaterialAsset>         Materials;      // material assets
    std::vector<std::string>           Textures;       // imported texture file paths
    std::vector<unsigned int>          TextureIDs;     // GL texture IDs (parallel to Textures)
    std::vector<int>                   TextureFolders; // folder index per texture (-1 = root)
    std::vector<ChannelVariable>       Channels;

    // --- Entity API ---
    Entity CreateEntity(const std::string& name = "Entity");
    void   OnUpdate(float dt);
    int    GetActiveGameCamera() const;

    // Move entity to be a child of another entity (-1 = unparent)
    void   SetEntityParent(int childIdx, int parentIdx);
    // Delete an entity by index (fixes all references)
    void   DeleteEntity(int idx);
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

    // --- Channels ---
    void EnsureChannelCount(int count);
    bool GetChannelBool(int idx, bool fallback = false) const;
    float GetChannelFloat(int idx, float fallback = 0.0f) const;
    std::string GetChannelString(int idx, const std::string& fallback = "") const;
    void SetChannelBool(int idx, bool value);
    void SetChannelFloat(int idx, float value);
    void SetChannelString(int idx, const std::string& value);
};

} // namespace tsu