#pragma once
#include <vector>
#include <string>
#include <map>
#include <cstdint>
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

// ================================================================
// TextureImportSettings — stored per imported texture
// ================================================================
struct TextureImportSettings
{
    bool  IsLinear   = false;  // linear load (normal/data maps); false = sRGB (albedo)
    int   WrapS      = 0;      // 0=Repeat  1=Clamp  2=MirroredRepeat
    int   WrapT      = 0;
    int   Filter     = 0;      // 0=Linear+Mipmaps  1=Nearest
    float Anisotropy = 4.0f;   // 1..16
};

struct MaterialAsset
{
    std::string  Name   = "Material";
    int          Folder = -1;                   // index into scene-level folders, -1 = root
    bool         Hidden = false;                // auto-generated materials hidden from asset browser
    glm::vec3    Color  = {1.0f, 1.0f, 1.0f};  // tint applied on top of albedo

    // ---- PBR texture paths (all optional) ----
    std::string  AlbedoPath;      // replaces legacy TexturePath
    std::string  NormalPath;
    std::string  AOPath;          // source for ORM.R  (ambient occlusion, grey-scale)
    std::string  RoughnessPath;   // source for ORM.G  (grey-scale)
    std::string  MetallicPath;    // source for ORM.B  (grey-scale)

    // ---- PBR scalar fallbacks (used when no texture is assigned) ----
    float        Roughness = 0.5f;
    float        Metallic  = 0.0f;
    float        AOValue   = 1.0f;

    // ---- Runtime GL handles (not serialised) ----
    unsigned int AlbedoID  = 0;   // GL_TEXTURE0
    unsigned int NormalID  = 0;   // GL_TEXTURE2
    unsigned int ORMID     = 0;   // GL_TEXTURE3 — auto-packed from AO/Roughness/Metallic paths
    bool         ORM_Dirty = true; // repack ORM before next draw

    // ---- UV mapping ----
    glm::vec2    Tiling      = {1.0f, 1.0f}; // UV repetitions: x scales XZ axes, y scales Y axis
    bool         WorldSpaceUV = false;        // true = world-position triplanar (seamless across objects)
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

    // ---- Headbob ----
    bool  HeadbobEnabled       = true;
    float HeadbobFrequency     = 2.2f;    // cycles per second at walk speed
    float HeadbobAmplitudeY    = 0.055f;  // vertical bob amplitude (world units)
    float HeadbobAmplitudeX    = 0.030f;  // lateral roll amplitude (world units)
    int   HeadbobCameraEntity  = -1;      // entity to apply bob to (-1 = disabled)
    // Runtime (not serialised)
    float _HeadbobPhase    = 0.0f;
    float _HeadbobBlend    = 0.0f;   // 0=still 1=fullbob (smoothed)
    float _HeadbobBaseY    = 0.0f;   // captures the camera's rest Y on first move
    float _HeadbobBaseX    = 0.0f;
    bool  _HeadbobBaseSet  = false;
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
    bool      BakedOnly   = false;    // excluded from real-time; contributes only to lightmap bakes
};

// Forward declarations for animation controller data (defined later)
struct AnimationStateData;
struct AnimationTransitionData;

// ================================================================
// PrefabAsset  --  serialisable entity template stored in assets
//   Nodes[0]  = root entity
//   Nodes[n]  = child entity (ParentIdx points to index in Nodes)
// ================================================================
struct PrefabEntityData
{
    std::string         Name;
    int                 ParentIdx    = -1;  // index in PrefabAsset::Nodes; -1 = root
    int                 NodeGuid     = -1;
    std::string         MeshType;
    std::string         MaterialName;
    TransformComponent  Transform;
    bool                EntityStatic = true;
    bool                ReceiveLightmap = true;
    std::string         Tag;
    bool                HasLight     = false;
    LightComponent      Light;
    bool                HasCamera    = false;
    GameCameraComponent Camera;
    bool                HasTrigger   = false;
    glm::vec3           TriggerSize  = {1.0f, 2.0f, 1.0f};
    glm::vec3           TriggerOffset = {0.0f, 0.0f, 0.0f};
    int                 TriggerChannel = -1;
    bool                TriggerInsideValue = true;
    bool                TriggerOutsideValue = false;
    bool                TriggerOneShot = false;
    bool                HasAnimator = false;
    int                 AnimProp = 0;
    int                 AnimMode = 0;
    int                 AnimEasing = 0;
    glm::vec3           AnimFrom = {0.0f, 0.0f, 0.0f};
    glm::vec3           AnimTo = {0.0f, 1.0f, 0.0f};
    float               AnimDuration = 1.0f;
    float               AnimDelay = 0.0f;
    float               AnimSpeed = 1.0f;
    float               AnimBlend = 1.0f;
    bool                AnimAutoPlay = true;
    bool                HasLuaScript = false;
    std::string         LuaScriptPath;
    std::map<std::string, std::string> LuaExposedVars;
    bool                HasAudioSource = false;
    std::string         AudioClipPath;
    bool                AudioLoop = true;
    bool                AudioPlayOnStart = true;
    bool                AudioSpatial = true;
    float               AudioVolume = 1.0f;
    float               AudioPitch = 1.0f;
    float               AudioMinDistance = 1.0f;
    float               AudioMaxDistance = 25.0f;
    float               AudioOcclusionFactor = 0.65f;
    int                 AudioOutputChannel = -1;
    bool                HasAudioBarrier = false;
    glm::vec3           AudioBarrierSize = {1.0f, 1.0f, 1.0f};
    glm::vec3           AudioBarrierOffset = {0.0f, 0.0f, 0.0f};
    float               AudioBarrierOcclusion = 0.55f;
    bool                HasMpManager = false;
    bool                MpMgrEnabled = true;
    int                 MpMgrMode    = 0; // 0=Host 1=Client
    std::string         MpMgrBind    = "0.0.0.0";
    std::string         MpMgrServer  = "127.0.0.1";
    int                 MpMgrPort    = 27015;
    int                 MpMgrMaxClients = 16;
    float               MpMgrSnapshotRate = 20.0f;
    bool                MpMgrReplicateChannels = true;
    bool                MpMgrAutoStart = true;
    bool                MpMgrAutoReconnect = true;
    float               MpMgrReconnectDelay = 2.0f;
    bool                HasMpController = false;
    bool                MpCtrlEnabled   = true;
    std::string         MpCtrlNickname  = "Player";
    uint64_t            MpCtrlNetworkId = 0;
    bool                MpCtrlIsLocal   = false;
    bool                MpCtrlSyncTransform = true;
    bool                HasAnimController = false;
    bool                AcEnabled       = true;
    bool                AcAutoStart     = true;
    int                 AcDefaultState  = 0;
    std::vector<AnimationStateData>      AcStates;
    std::vector<AnimationTransitionData> AcTransitions;
};

// ================================================================
// Procedural Maze System  -- room templates, sets & generator config
// ================================================================

// A single 1×1×1 block in a room grid
struct RoomBlock
{
    int X = 0, Y = 0, Z = 0;  // grid position (can be negative)
};

// Which face of a block and at which grid coord the door sits
enum class DoorFace { PosX = 0, NegX = 1, PosZ = 2, NegZ = 3 };

struct DoorPlacement
{
    int      BlockX = 0, BlockZ = 0;   // which block column
    int      BlockY = 0;               // vertical layer (floor level)
    DoorFace Face   = DoorFace::PosX;
};

// Room editor gizmo: one exposed-face arrow per block face (rebuilt each frame)
struct BlockFaceArrow {
    glm::vec3 pos;      // world position (block center + 0.6 * faceNormal)
    int       dir;      // 0=+X 1=-X 2=+Z 3=-Z 4=+Y 5=-Y
    int       blockIdx; // index into RoomTemplate::Blocks
};

// Room template: a set of blocks + doors + optional interior prefab nodes
struct RoomTemplate
{
    std::string  Name = "Room";
    std::vector<RoomBlock>       Blocks;       // occupied grid cells
    std::vector<DoorPlacement>   Doors;        // door locations
    std::vector<PrefabEntityData> Interior;    // decoration / props (prefab-style nodes)
    float Rarity = 1.0f;                       // weight used during random selection

    // ---- Prebaked lightmap ----
    std::string  LightmapPath;   // relative path to baked .tga file; "" = not baked
    glm::vec4    LightmapST = {1,1,0,0}; // xy=scale, zw=offset to convert world-XZ → UV
};

// Room set: named collection of room templates
struct RoomSet
{
    std::string                Name = "Room Set";
    std::vector<RoomTemplate>  Rooms;
};

// Maze generator component (attached to "generator" entities)
struct MazeGeneratorComponent
{
    bool  Active  = false;
    bool  Enabled = true;
    int   RoomSetIndex  = -1;       // which RoomSet to use (-1 = none)
    float GenerateRadius = 50.0f;   // spawn rooms within this radius of the player
    float DespawnRadius  = 80.0f;   // destroy rooms beyond this radius
    float BlockSize      = 2.0f;    // world-space size of one grid block

    // Runtime state (not serialised)
    struct LiveRoom
    {
        int   TemplateIdx = -1;     // index in RoomSet::Rooms
        int   GridX = 0, GridZ = 0; // room anchor in world-grid coords
        int   Rotation = 0;         // 0/90/180/270
        std::vector<int> EntityIds; // spawned entity indices in the scene
        std::string LightmapPath;   // "" = no lightmap (for release on despawn)
        // Grid cells registered as pending door exits when this room was spawned.
        // Removed from PendingDoorSlots when this room despawns.
        std::vector<std::pair<int,int>> DoorExitCells;
    };

    // A pending door exit: a grid position that needs a connecting room with a
    // specific door on one of its faces (the opposite face to the outgoing door).
    struct DoorSlot
    {
        int      gridX        = 0;
        int      gridZ        = 0;
        DoorFace requiredFace = DoorFace::PosX; // the face the new room must have a door on
    };

    std::vector<LiveRoom>            SpawnedRooms;
    // Occupied world-grid cells (for overlap prevention). Key = packed (gx,gz).
    std::vector<std::pair<int,int>>  OccupiedCells;
    // Open door exits: grid positions awaiting connection by a matching room.
    std::vector<DoorSlot>            PendingDoorSlots;
};

// ================================================================
// FogSettings  --  per-scene atmospheric fog
// ================================================================
enum class FogType { None = 0, Linear = 1, Exponential = 2, Exp2 = 3 };

struct FogSettings
{
    FogType   Type    = FogType::None;
    glm::vec3 Color   = { 0.5f, 0.5f, 0.5f };
    float     Density = 0.05f;   // for Exp / Exp2 modes
    float     Start   = 10.0f;  // for Linear mode
    float     End     = 60.0f;  // for Linear mode
};

// ================================================================
// SkySettings  --  procedural gradient sky + sun disk
// ================================================================
struct SkySettings
{
    bool      Enabled      = false;
    glm::vec3 ZenithColor  = { 0.09f, 0.20f, 0.45f };  // top of sky
    glm::vec3 HorizonColor = { 0.60f, 0.75f, 0.90f };  // horizon band
    glm::vec3 GroundColor  = { 0.15f, 0.13f, 0.10f };  // below horizon
    glm::vec3 SunDirection = { 0.40f, 0.80f, 0.30f };  // sun direction (world space, normalized)
    glm::vec3 SunColor     = { 1.5f, 1.4f, 1.0f };     // HDR sun color
    float     SunSize      = 0.015f;                    // cos of half-angle (smaller = tighter disc)
    float     SunBloom     = 0.12f;                     // glow radius around the disc
};

// ================================================================
// PostProcessSettings  --  fullscreen FBO post-processing
// ================================================================
struct PostProcessSettings
{
    bool  Enabled         = false;

    // Bloom
    bool  BloomEnabled    = true;
    float BloomThreshold  = 0.85f;
    float BloomIntensity  = 0.40f;
    float BloomRadius     = 1.5f;    // texel spread multiplier

    // Vignette
    bool  VignetteEnabled = true;
    float VignetteRadius  = 0.70f;   // 0=center 1=corners fully dark
    float VignetteStrength= 0.55f;

    // Film grain
    bool  GrainEnabled    = false;
    float GrainStrength   = 0.04f;

    // Chromatic aberration
    bool  ChromaEnabled   = false;
    float ChromaStrength  = 0.003f;

    // Color grading
    bool  ColorGradeEnabled = true;
    float Exposure        = 1.0f;
    float Saturation      = 1.0f;   // 0=greyscale 1=normal >1=vivid
    float Contrast        = 1.0f;
    float Brightness      = 0.0f;   // additive offset

    // Sharpen (unsharp mask kernel)
    bool  SharpenEnabled  = false;
    float SharpenStrength = 0.5f;

    // Lens distortion (barrel / pincushion)
    bool  LensDistortEnabled  = false;
    float LensDistortStrength = 0.3f;  // positive = barrel, negative = pincushion

    // Sepia tone
    bool  SepiaEnabled    = false;
    float SepiaStrength   = 1.0f;

    // Posterize (quantise color levels)
    bool  PosterizeEnabled = false;
    float PosterizeLevels  = 5.0f;   // number of discrete levels per channel

    // Scanlines
    bool  ScanlinesEnabled   = false;
    float ScanlinesStrength  = 0.35f;
    float ScanlinesFrequency = 800.0f; // lines per screen height

    // Pixelate
    bool  PixelateEnabled  = false;
    float PixelateSize     = 4.0f;   // pixel block size in screen pixels
    bool  SSAOEnabled      = false;
    float SSAOIntensity    = 0.35f;
    float SSAORadius       = 2.0f;
    bool  UseExternalPostShader = false;
    std::string ExternalPostPath;
    bool  UseExternalColorCfg    = false;
    std::string ExternalColorCfgPath;
    bool  LUTEnabled       = false;
    std::string LUTPath;
    float LUTStrength      = 1.0f;
};

struct RealtimeRenderSettings
{
    bool  EnableShadows = true;
    int   ShadowMapSize2D = 1024;
    int   ShadowMapSizeCube = 512;
    float DirectionalShadowDistance = 40.0f;
    float DirectionalShadowOrtho = 30.0f;
    int   MaxRealtimeLights = 8;
    bool  FrustumCulling = true;
    bool  DistanceCulling = false;
    float MaxDrawDistance = 150.0f;
    bool  UseFog = true;
    bool  UseSkyAmbient = true;
};

struct BakedLightingSettings
{
    bool  AutoApplyLightmapIntensity = true;
    float AutoIntensityMultiplier = 1.0f;
    float MaxAutoIntensity = 2.0f;
};

// ================================================================
// LuaScriptComponent  --  Lua script attached to an entity
// ================================================================
struct LuaScriptComponent
{
    bool        Active     = false;
    bool        Enabled    = true;
    std::string ScriptPath;   // path relative to assets/scripts/ (or absolute)
    std::map<std::string, std::string> ExposedVars; // variables exposed to inspector via --@expose
};

// ================================================================
// TriggerComponent  --  an AABB that fires channel events on player overlap
// ================================================================
struct TriggerComponent
{
    bool      Active          = false;
    bool      Enabled         = true;
    glm::vec3 Size            = { 1.0f, 2.0f, 1.0f }; // box extents (half-size)
    glm::vec3 Offset          = { 0.0f, 0.0f, 0.0f };

    // A single channel is set while the player is inside / outside
    int       Channel         = -1;   // -1 = no action
    bool      ActiveValue     = true;
    bool      InsideValue     = true;  // value to set when inside
    bool      OutsideValue    = false; // value to set when outside

    // One-shot: only fires once, re-armed with Reset
    bool      OneShot         = false;
    bool      _HasFired       = false; // runtime

    // Visual debug
    bool      ShowTrigger     = false;

    // Runtime overlap tracking
    bool      _PlayerInside   = false;
};

// ================================================================
// AnimatorComponent  --  simple keyframe animator for position / rotation / scale
// ================================================================
enum class AnimatorProperty { Position = 0, Rotation = 1, Scale = 2 };
enum class AnimatorEasing   { Linear = 0, EaseIn = 1, EaseOut = 2, EaseInOut = 3 };
enum class AnimatorMode     { Oscillate = 0, OneShot = 1, Loop = 2 };

struct AnimatorComponent
{
    bool              Active    = false;
    bool              Enabled   = true;
    bool              Playing   = false; // set to true to start
    bool              AutoPlay  = false; // starts playing when game mode begins

    AnimatorProperty  Property  = AnimatorProperty::Position;
    AnimatorMode      Mode      = AnimatorMode::Oscillate;
    AnimatorEasing    Easing    = AnimatorEasing::EaseInOut;

    glm::vec3         From      = { 0.0f, 0.0f, 0.0f };
    glm::vec3         To        = { 0.0f, 1.0f, 0.0f };
    float             Duration  = 1.0f;   // seconds for one half-cycle
    float             Delay     = 0.0f;   // initial delay before starting
    float             PlaybackSpeed = 1.0f;
    float             BlendWeight   = 1.0f;

    // Runtime state (not serialised)
    float             _Phase    = 0.0f;  // [0,1] animated parameter
    float             _Dir      = 1.0f;  // +1 / -1 for oscillate
    float             _Timer    = 0.0f;  // accumulated time
    bool              _DelayDone = false;
};

enum class AnimationConditionOp { BoolTrue = 0, BoolFalse = 1, FloatGreater = 2, FloatLess = 3 };

struct AnimationStateData
{
    std::string       Name       = "State";
    AnimatorProperty  Property   = AnimatorProperty::Position;
    AnimatorMode      Mode       = AnimatorMode::Loop;
    AnimatorEasing    Easing     = AnimatorEasing::EaseInOut;
    glm::vec3         From       = {0,0,0};
    glm::vec3         To         = {0,1,0};
    float             Duration   = 1.0f;
    float             Delay      = 0.0f;
    float             PlaybackSpeed = 1.0f;
    float             BlendWeight   = 1.0f;
    bool              AutoPlay      = true;
};

struct AnimationTransitionData
{
    int                  FromState = -1;
    int                  ToState   = -1;
    int                  Channel   = -1;
    AnimationConditionOp Condition = AnimationConditionOp::BoolTrue;
    float                Threshold = 0.5f;
    float                ExitTime  = 0.0f;
};

struct AnimationControllerComponent
{
    bool                                Active = false;
    bool                                Enabled = true;
    bool                                AutoStart = true;
    int                                 DefaultState = 0;
    std::vector<AnimationStateData>     States;
    std::vector<AnimationTransitionData> Transitions;
    int                                 _CurrentState = -1;
    float                               _StateTime = 0.0f;
};

// ================================================================
// Skinned Mesh Component  -- simple CPU skinning pipeline
// ================================================================
struct SkinnedMeshComponent
{
    bool        Active     = false;
    bool        Enabled    = true;
    std::string SkeletonPath;
    bool        Loaded     = false;
    int         BoneCount  = 0;
    std::vector<int>        Parent;
    std::vector<glm::mat4>  BindPose;
    std::vector<glm::mat4>  Pose;
    std::vector<glm::ivec4> BoneIndices; // per-vertex (size == mesh vertex count)
    std::vector<glm::vec4>  BoneWeights; // per-vertex
    std::vector<Mesh::CpuVertex> BaseVertices; // mesh original vertices
};

enum class MultiplayerMode
{
    Host = 0,
    Client = 1
};

struct MultiplayerManagerComponent
{
    bool        Active = false;
    bool        Enabled = true;
    bool        AutoStart = true;
    MultiplayerMode Mode = MultiplayerMode::Host;
    std::string BindAddress = "0.0.0.0";
    std::string ServerAddress = "127.0.0.1";
    int         Port = 27015;
    int         MaxClients = 16;
    float       SnapshotRate = 20.0f;
    bool        ReplicateChannels = true;
    bool        Running = false;
    bool        AutoReconnect = true;
    float       ReconnectDelay = 2.0f;
    bool        Connected = false;
    int         ConnectedPlayers = 0;
    float       EstimatedPingMs = 0.0f;
    float       TimeSinceLastPacket = 0.0f;
    std::string RuntimeState = "Stopped";
    int         PlayerPrefabIdx = -1;   // index into Scene::Prefabs (-1 = none)
    glm::vec3   SpawnPoint = {0, 1, 0}; // where to spawn players
    bool        RequestStartHost = false;
    bool        RequestStartClient = false;
    bool        RequestStop = false;

    // Runtime: indices of entities spawned by this manager (cleaned up on stop)
    std::vector<int> SpawnedEntities;
};

struct MultiplayerControllerComponent
{
    bool        Active = false;
    bool        Enabled = true;
    std::string Nickname = "Player";
    uint64_t    NetworkId = 0;
    bool        IsLocalPlayer = false;
    bool        SyncTransform = true;
    bool        Replicated = false;
    bool        OwnedLocally = false;
    float       LastNetUpdateAge = -1.0f;
    float       LastNetReceiveTime = -1.0f;
};

struct AudioSourceComponent
{
    bool        Active          = false;
    bool        Enabled         = true;
    std::string ClipPath;
    bool        Loop            = true;
    bool        PlayOnStart     = true;
    bool        Spatial         = true;
    float       Volume          = 1.0f;
    float       Pitch           = 1.0f;
    float       MinDistance     = 1.0f;
    float       MaxDistance     = 25.0f;
    float       OcclusionFactor = 0.65f;
    int         OutputChannel   = -1;
    int         GateChannel     = -1;
    bool        GateValue       = true;
    bool        TriggerOneShot  = false;
    bool        _GatePrev       = false;
    bool        _Playing        = false;
    float       _CurrentGain    = 0.0f;
};

struct AudioBarrierComponent
{
    bool      Active    = false;
    bool      Enabled   = true;
    glm::vec3 Size      = {1.0f, 1.0f, 1.0f};
    glm::vec3 Offset    = {0.0f, 0.0f, 0.0f};
    float     Occlusion = 0.55f;
};

// ================================================================
// PrefabAsset  --  serialisable entity template stored in assets
// ================================================================
struct PrefabAsset
{
    std::string                   Name     = "Prefab";
    std::string                   FilePath;
    int                           Folder   = -1;
    std::vector<PrefabEntityData> Nodes;   // Nodes[0] is root; empty = invalid
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
    std::vector<bool>                  EntityStatic;   // bake candidate: static geometry
    std::vector<bool>                  EntityReceiveLightmap; // receives baked lighting
    std::vector<PlayerControllerComponent> PlayerControllers;
    std::vector<MouseLookComponent>         MouseLooks;
    std::vector<LightComponent>             Lights;
    std::vector<TriggerComponent>           Triggers;
    std::vector<AnimatorComponent>          Animators;
    std::vector<AnimationControllerComponent> AnimationControllers;
    std::vector<MultiplayerManagerComponent>  MultiplayerManagers;
    std::vector<MultiplayerControllerComponent> MultiplayerControllers;
    std::vector<LuaScriptComponent>         LuaScripts;
    std::vector<AudioSourceComponent>       AudioSources;
    std::vector<AudioBarrierComponent>      AudioBarriers;
    std::vector<SkinnedMeshComponent>   SkinnedMeshes;

    // --- Scene-level settings ---
    FogSettings         Fog;
    SkySettings         Sky;
    PostProcessSettings PostProcess;
    RealtimeRenderSettings Realtime;
    BakedLightingSettings  BakedLighting;
    float               LightmapIntensity = 1.0f;  // baked GI multiplier (0=off, 1=default, >1=stronger)

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
    std::vector<TextureImportSettings>  TextureSettings; // import settings per texture (parallel)
    std::vector<ChannelVariable>       Channels;
    std::vector<PrefabAsset>           Prefabs;                // prefab assets
    std::vector<bool>                  EntityIsPrefabInstance; // parallel to Transforms
    std::vector<int>                   EntityPrefabSource;     // parallel: index into Prefabs (-1 = none)
    std::vector<int>                   EntityPrefabNode;       // parallel: index into PrefabAsset::Nodes (-1 = none)
    std::vector<std::string>           EntityTags;             // per-entity tag string
    std::vector<std::string>           MeshAssets;             // OBJ file paths registered as assets
    std::vector<int>                   MeshAssetFolders;       // folder index per mesh asset (-1 = root)
    std::vector<std::string>           ScriptAssets;           // Lua script file paths registered as assets
    std::vector<std::string>           ShaderAssets;           // Shader file paths (.glsl/.frag/.vert)
    std::vector<std::string>           AudioAssets;            // Audio clip file paths (.wav/.ogg/.mp3)
    std::vector<int>                   ScriptAssetFolders;     // folder index per script asset (-1 = root)

    // --- Maze system ---
    std::vector<MazeGeneratorComponent> MazeGenerators;        // parallel per-entity
    std::vector<RoomSet>                RoomSets;              // scene-level room set assets

    // --- Editor camera state (not used in gameplay, saved per-scene) ---
    glm::vec3 EditorCamPos   = { 0.0f, 1.0f, 6.0f };
    float     EditorCamYaw   = -90.0f;
    float     EditorCamPitch =   0.0f;

    // --- Entity API ---
    Entity CreateEntity(const std::string& name = "Entity");
    // Instantiate a prefab into the scene, returns root entity index (-1 on failure)
    int    SpawnFromPrefab(int prefabIdx, const glm::vec3& position);
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
