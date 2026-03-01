# Tsu Game Engine

Tsu Game Engine is a 3D engine written in C++ that originated from the need to build a very specific game concept.

The project did not begin as a learning exercise. It started because I wanted to create a particular type of game that required a level of architectural control, modularity, and lighting behavior that I could not realistically achieve using existing game engines. Building a custom engine became the only practical solution.

As development progressed, the project naturally evolved into a deeper exploration of engine architecture. What began as a technical necessity gradually became both a study of how modern engines work internally and a decision to intentionally grow it into a fully structured game engine.

## Vision

Tsu is being built as a modular and highly controlled 3D engine with its own editor environment. The goal is to maintain full authority over the rendering pipeline, scene structure, and system design instead of relying on pre-built abstractions.

The engine is specifically focused on generating high graphical quality experiences built around procedural, module-based labyrinth generation. The core idea is to construct environments from predefined modular pieces and, in the future, precalculate all possible lighting permutations between those modules.

By precalculating lighting interactions across modular configurations, the engine aims to significantly reduce real-time lighting costs while preserving visual fidelity.

Performance and optimization are central priorities. The architecture is being designed with careful attention to efficiency, including optimization strategies specifically targeting AMD hardware.

## Intended Features

Tsu Game Engine is planned to include:

- A modular Entity Component System (ECS)
- A custom editor with scene manipulation tools
- Clear separation between Editor Mode and runtime Game Mode
- Visual transform gizmos (move, rotate, scale)
- A modular scripting system
- Scene serialization
- A material and shader system
- A module-based procedural generation pipeline
- A lighting system capable of precomputed modular light permutations
- A rendering pipeline designed for high performance and hardware-aware optimization

## Purpose

Tsu is both a purpose-driven solution for a specific game design challenge and an evolving engine architecture project. What began as a necessity to enable a game concept has grown into a deliberate effort to design and build a fully independent game engine from the ground up.

---

## Technical Stack

| Layer | Technology |
|---|---|
| Language | C++17 |
| Build | CMake |
| Graphics API | OpenGL 3.3 Core Profile |
| GL Loader | GLAD |
| Window / Input | GLFW |
| Editor UI | Dear ImGui v1.91.6 |
| Math | GLM |

---

## Engine Conventions

These conventions apply throughout the entire codebase.

| Axis | Role |
|---|---|
| **+X** | Entity local **forward** |
| **+Y** | World and local **up** |
| **+Z** | Entity local **right** |

Rotations are stored in degrees and applied **Rx then Ry then Rz** (roll, yaw, pitch) as defined in `TransformComponent::GetMatrix()`.

**Pitch** (looking up and down) is `Rotation.x`.

---

## Directory Structure

```
tsuEngine/
 CMakeLists.txt
 assets/scenes/          - saved .tscene files
 engine/
    components/         - component struct headers
    core/               - Application loop, Window
    editor/             - EditorCamera, EditorGizmo
    input/              - InputManager
    physics/            - PhysicsSystem
    renderer/           - Renderer, Mesh, TextureLoader
    scene/              - Scene, Entity
    serialization/      - SceneSerializer
    ui/                 - HierarchyPanel, InspectorPanel
 external/               - GLAD, GLFW, GLM, stb
 sandbox/main.cpp        - entry point
 shaders/                - vertex.glsl, fragment.glsl
```

---

## Scene and Entity System

`engine/scene/scene.h` and `scene.cpp`

All scene data lives in **parallel arrays**. Every entity is an integer index. A component is present when its `Active` flag is `true`.

```
Index 0  ->  Transforms[0], MeshRenderers[0], RigidBodies[0], PlayerControllers[0] ...
Index 1  ->  Transforms[1], MeshRenderers[1], RigidBodies[1], PlayerControllers[1] ...
```

Key `Scene` methods:

| Method | Description |
|---|---|
| `GetEntityWorldMatrix(i)` | Walks the parent chain and returns the full world TRS matrix for entity i |
| `GetEntityWorldPos(i)` | World position as glm::vec3 |
| `GetActiveGameCamera()` | Index of the first active GameCameraComponent, or -1 |
| `GetChannelString(ch)` | StringValue of a channel (used by Channels input mode) |
| `EnsureSize(n)` | Grows all arrays to hold at least n entities |

**SceneGroup** - hierarchy folders. Groups carry their own `TransformComponent` (local to parent group) so child entities inherit the group world transform.

---

## Components

### TransformComponent

`engine/components/transformComponent.h`

| Field | Type | Default | Description |
|---|---|---|---|
| `Position` | glm::vec3 | {0,0,0} | Local position |
| `Rotation` | glm::vec3 | {0,0,0} | Local Euler rotation in degrees. X=roll, Y=yaw, Z=pitch |
| `Scale` | glm::vec3 | {1,1,1} | Local scale |
| `Parent` | int | -1 | Parent entity index. -1 means no parent |

`GetMatrix()` returns a local TRS matrix. World matrix is the product of the full parent chain via `Scene::GetEntityWorldMatrix()`.

---

### MeshRendererComponent

`engine/components/meshRendererComponent.h`

| Field | Type | Description |
|---|---|---|
| `MeshPtr` | Mesh* | Pointer to an engine mesh (Box, Sphere, Capsule, Cylinder, Plane, and others) |
| `Active` | bool | Whether the mesh is rendered this frame |

---

### GameCameraComponent

`engine/components/cameraComponent.h`

Perspective camera that drives the in-game view.

| Field | Default | Description |
|---|---|---|
| `FOV` | 75 | Vertical field of view in degrees |
| `Near` / `Far` | 0.1 / 500 | Clipping planes |
| `Active` | false | Only the first active camera is rendered per frame |

`GetProjection(float aspect)` returns a glm::perspective matrix. The game view direction is derived from the entity world transform. The camera looks along its world **-Z** axis.

---

### RigidBodyComponent

`engine/scene/scene.h`

Three optional sub-modules toggled independently via the inspector.

#### Gravity Module

| Field | Default | Description |
|---|---|---|
| `UseGravity` | true | Apply downward acceleration each tick |
| `IsKinematic` | false | Kinematic bodies move by code only, not forces |
| `Mass` | 1.0 | Kilograms. Affects impulse magnitude on collision |
| `Drag` | 0.01 | Linear velocity damping per tick |
| `FallSpeedMultiplier` | 1.0 | Scale factor applied to gravitational acceleration |
| `Velocity` | {0,0,0} | Current world-space velocity in meters per second |
| `IsGrounded` | false | True when resting on a surface |

#### Collider Module

| Field | Default | Description |
|---|---|---|
| `Collider` | Box | Shape: Box, Sphere, Capsule, Pyramid, or None |
| `ColliderSize` | {1,1,1} | Full extents for Box colliders |
| `ColliderRadius` | 0.5 | Radius for Sphere and Capsule |
| `ColliderHeight` | 1.0 | Cylinder height for Capsule |
| `ColliderOffset` | {0,0,0} | Local offset from the entity origin |
| `ShowCollider` | false | Renders a green wireframe overlay in the editor |

#### RigidBody Mode

Full simulation including angular velocity from impacts.

| Field | Default | Description |
|---|---|---|
| `AngularVelocity` | {0,0,0} | Rotational velocity in degrees per second |
| `AngularDamping` | 0.0 | 0 = no damping, 1 = instant stop |
| `Restitution` | 0.25 | Bounciness coefficient. 0 = no bounce, 1 = perfect elastic |
| `FrictionCoef` | 0.15 | Lateral friction applied on collision impact |

---

### PlayerControllerComponent

`engine/scene/scene.h`

Character controller with movement and optional built-in mouse look.

#### Movement Fields

| Field | Default | Description |
|---|---|---|
| `WalkSpeed` | 4.0 | Base movement speed in units per second |
| `RunMultiplier` | 1.8 | Speed multiplier while run key is held |
| `CrouchMultiplier` | 0.45 | Speed multiplier while crouch key is held |
| `AllowRun` / `AllowCrouch` | true | External flags to block these actions |

#### Movement Mode

| Mode | Name | Description |
|---|---|---|
| 1 | WorldWithCollision | Move along world-space X/Z axes. Entity rotation is ignored. Physics collision active. |
| **2** | **LocalWithCollision** | Move relative to the entity facing direction. Forward always goes where the entity looks. Physics collision active. This is the default. |
| 3 | WorldNoCollision | World-space movement, noclip. Collision response bypassed. |
| 4 | LocalNoCollision | Facing-relative movement, noclip. |

#### Input Mode

| Mode | Description |
|---|---|
| Local | Reads GLFW key codes from KeyForward, KeyBack, KeyLeft, KeyRight, KeyRun, KeyCrouch |
| Channels | Each action maps to a channel index. The channel holds the key name as a string |

Default local bindings: W forward, S back, A left, D right, Left Shift run, Left Control crouch.

Runtime read-only fields: `IsRunning`, `IsCrouching`, `LastMoveAxis`.

---

### MouseLookComponent

`engine/scene/scene.h`

Standalone mouse look for cases where yaw/pitch rotation is needed independent of a PlayerController.

| Field | Description |
|---|---|
| `YawTargetEntity` | Entity rotated around world Y for horizontal look |
| `PitchTargetEntity` | Entity rotated around **local X** for pitch. |
| `SensitivityX` / `SensitivityY` | Per-axis mouse sensitivity |
| `InvertX` / `InvertY` | Flip axis direction |
| `ClampPitch` | Enable pitch angle clamping |
| `PitchMin` / `PitchMax` | Clamp range, recommend -89 to 89 degrees |
| `CurrentYaw` / `CurrentPitch` | Accumulated angles in degrees, read-only at runtime |

---

### LightComponent

`engine/scene/scene.h`

| Type | Description |
|---|---|
| Directional | Sun-like infinite light. No position, only direction from entity rotation. Used as the shadow map caster. |
| Point | Omnidirectional. Falls off with distance up to Range. |
| Spot | Cone light. Full brightness inside InnerAngle, smooth falloff to OuterAngle. Also casts shadow maps. |
| Area | Rectangular area source defined by Width and Height. |

| Field | Default | Description |
|---|---|---|
| `Color` | {1,1,1} | RGB multiplier |
| `Temperature` | 6500 | Color temperature in Kelvin, converted to an RGB tint |
| `Intensity` | 1.0 | Brightness scalar |
| `Range` | 10.0 | Maximum influence radius for Point, Spot, and Area lights |
| `InnerAngle` | 30 | Spot full-brightness cone half-angle in degrees |
| `OuterAngle` | 45 | Spot falloff-edge cone half-angle in degrees |

Up to **8 active lights** are passed to the shader per frame.

---

## Renderer

`engine/renderer/renderer.h` and `renderer.cpp`

Static class. All rendering goes through it.

### Per-frame Pipeline

1. **Shadow Pass** (`RenderShadowPass`). Finds the first active Directional or Spot light. Renders all meshes to a 2048x2048 depth-only framebuffer from the light point of view. Front-face culling prevents self-shadow artifacts.

2. **Scene Pass** (`DrawScene`). Forward-renders all mesh entities. Passes up to 8 lights as uniforms. Binds the shadow depth texture to GL_TEXTURE1 when a shadow caster was found.

3. **Gizmos** (editor only). Collider wireframes in green, game camera gizmos (sphere plus forward arrow).

### Shadow Mapping

| Property | Value |
|---|---|
| Resolution | 2048 x 2048 |
| Filter | PCF 3x3 kernel |
| Bias | adaptive: max(0.005 * (1 - N dot L), 0.0005) |
| Max shadow strength | 80 percent. Ambient is always at least 20 percent. |
| Outside map | No shadow. Border clamp color is 1.0. |
| Directional projection | Orthographic plus/minus 30 units, eye at -forward * 40 |
| Spot projection | Perspective. FOV = OuterAngle * 2 |

### Lighting Model

Diffuse plus constant ambient in GLSL.

- Ambient floor: vec3(0.07)
- Fallback: one soft default directional light is used when no lights are in the scene
- Temperature: Kelvin converted to approximate RGB using a piecewise formula, multiplied into the light color uniform before passing to the shader

### Public API

| Method | Description |
|---|---|
| `Init()` | Compile shaders, build shadow FBO, initialize gizmo meshes |
| `BeginFrame()` | Clear the framebuffer |
| `RenderSceneEditor(scene, cam, w, h)` | Full editor render: shadow, scene, gizmos |
| `RenderSceneGame(scene, w, h)` | Game render from the active game camera |
| `RenderToolbar(playing, paused, w, h)` | Play and pause HUD bar |
| `DrawTranslationGizmo(pos, axis, cam, w, h)` | Draw the 3-axis translation gizmo |

---

## Physics System

`engine/physics/physicsSystem.h` and `physicsSystem.cpp`

Per-tick simulation over all entities with active RigidBodyComponent modules.

**Gravity** applies 9.81 * FallSpeedMultiplier downward to Velocity.y each tick. Drag is applied as `Velocity *= (1 - Drag)`.

**Collision detection:**

| Shape | Method |
|---|---|
| Box | AABB overlap |
| Sphere | Centre-to-centre distance |
| Capsule | Distance to line segment |
| Pyramid | Bounding AABB approximation |
| None | No collision |

On collision the velocity component along the surface normal is reflected by Restitution. The entity is depenetrated and IsGrounded is set when resting on a surface. Friction reduces lateral velocity on impact.

**RigidBody Mode** accumulates angular velocity from impacts. AngularDamping bleeds it per tick.

---

## Input System

`engine/input/inputManager.h` and `inputManager.cpp`

| Method | Description |
|---|---|
| `IsKeyPressed(code)` | True while key is held |
| `IsKeyJustPressed(code)` | True on the first frame a key transitions to down |
| `GetMouseDelta()` | Returns MouseDelta with dx and dy since last frame |
| `KeyNameToCode(name)` | Converts "W", "Space", "Left Shift" etc. to GLFW key code |
| `KeyCodeToName(code)` | Converts GLFW key code back to a human-readable name |

---

## Input Channels

Named variables stored in `Scene::Channels`. Each channel has a Name, a Type (Boolean, Float, or String), and a value. In Channels input mode the PlayerController maps each action (Forward, Back, Left, Right, Run, Crouch) to a channel index. The channel StringValue holds the key name such as "W". Channels are managed in the Project Settings panel and let input bindings be changed at runtime without recompiling.

---

## Editor

### Application and Main Loop

`engine/core/application.h` and `application.cpp`

| Method | Description |
|---|---|
| `UpdatePlayerControllers(dt)` | Reads input, moves all active PlayerControllers, applies built-in mouse look |
| `UpdateMouseLook(dt)` | Processes all standalone MouseLookComponent entities |
| `RaycastScene(mx, my, w, h)` | Casts a ray from the editor camera through screen pixel (mx, my) and returns the hit entity index |

Editor Mode renders the scene plus gizmos. Play Mode runs physics, controllers, then a game camera render.

### Editor Camera

`engine/editor/editorCamera.h` and `editorCamera.cpp`

Free-fly camera. Right-click and drag to look. WASD to fly. Scroll to zoom.

| Method | Description |
|---|---|
| `GetViewMatrix()` | lookAt view matrix |
| `GetProjection(aspect)` | Perspective projection matrix |
| `ProcessMouseMovement(dx, dy)` | Apply yaw and pitch deltas |
| `ProcessScroll(offset)` | Adjust zoom level |

### Editor Gizmo

`engine/editor/editorGizmo.h` and `editorGizmo.cpp`

3-axis translation gizmo for selected entities. HitTest detects which axis handle is under the cursor. Returns world-space drag deltas for translation.

### Hierarchy Panel

`engine/ui/hierarchyPanel.h` and `hierarchyPanel.cpp`

Left panel showing the full entity and group tree. Click to select. Drag to reorder. Click does not fire on drag. Inline rename. Create and delete entities and groups. Right-click context menu. Component type icons next to entity names.

### Inspector Panel

`engine/ui/inspectorPanel.h` and `inspectorPanel.cpp`

Right panel showing all components on the selected entity. Each component has a collapsible section with a header strip. Components can be removed via the X button on the header.

Sections and their key exposed fields:

| Section | Key fields |
|---|---|
| Transform | Position, Rotation in degrees, Scale |
| Mesh Renderer | Mesh type picker, material picker |
| Game Camera | FOV, Near, Far, Enabled toggle |
| Rigid Body | Gravity/Collider/RigidBody sub-modules with all fields, collider wireframe toggle |
| Player Controller | Movement mode, input mode, key bindings or channels, mouse look settings |
| Mouse Look | Yaw and pitch targets, sensitivity, invert, clamp settings |
| Light | Type, Color, Temperature, Intensity, Range, Inner and Outer angle |
| Add Component | Searchable popup to add any component to the entity |

---

## Scene Serialization

`engine/serialization/sceneSerializer.h` and `sceneSerializer.cpp`

Scenes are saved as plain-text `.tscene` files in `assets/scenes/`.

### File Format

```
[entity]
name = Player
pos  = 0.0 1.0 0.0
rot  = 0.0 0.0 0.0
scale = 1.0 1.0 1.0
mesh = capsule
player_controller = true
pc_move_mode = 2
pc_mode = local
pc_key_forward = 87
pc_key_back = 83
pc_key_left = 65
pc_key_right = 68

[entity]
name = Camera
parent = 0
pos = 0.0 0.8 0.0
game_camera = true
cam_fov = 75.0
```

### API

| Method | Description |
|---|---|
| `SaveScene(scene, path)` | Writes the entire scene to a .tscene file |
| `LoadScene(path, scene)` | Parses a .tscene file and populates the scene |

---

## Building

Requirements: CMake 3.20 or higher, a C++17 compiler (MSVC or GCC/Clang), OpenGL 3.3 capable GPU.

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/TsuEngine
```

GLFW is expected as a pre-built library under `external/glfw/`. GLAD, GLM, and stb are header-only and included in `external/`. ImGui is fetched automatically via CMake FetchContent.

---

## License

No license applied yet. All rights reserved.
