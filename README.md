# Tsu Game Engine

Tsu Game Engine is a 3D engine written in C++ that originated from the need to build a very specific game concept.

The project did not begin as a learning exercise. It started because I wanted to create a particular type of game that required a level of architectural control, modularity, and lighting behavior that I could not realistically achieve using existing game engines. Building a custom engine became the only practical solution.

As development progressed, the project naturally evolved into a deeper exploration of engine architecture. What began as a technical necessity gradually became both a study of how modern engines work internally and a decision to intentionally grow it into a fully structured game engine.

## Vision

Tsu is being built as a modular and highly controlled 3D engine with its own editor environment. The goal is to maintain full authority over the rendering pipeline, scene structure, and system design instead of relying on pre-built abstractions.

The engine is specifically focused on generating high graphical quality experiences built around procedural, module-based labyrinth generation. The core idea is to construct environments from predefined modular pieces and, in the future, precalculate all possible lighting permutations between those modules.

By precalculating lighting interactions across modular configurations, the engine aims to significantly reduce real-time lighting costs while preserving visual fidelity.

Performance and optimization are central priorities. The architecture is being designed with careful attention to efficiency, including optimization strategies specifically targeting AMD hardware.

## Current Features

- **PBR Rendering** — Cook-Torrance BRDF with GGX distribution, Smith geometry, Schlick fresnel
- **Multi-light system** — up to 8 simultaneous lights (Directional, Point, Spot, Area)
- **Dual shadow mapping** — 2D shadow maps for directional/spot lights + cubemap shadows for point lights, both with PCF filtering
- **PBR material system** — albedo, normal, and ORM (AO/Roughness/Metallic) texture support with triplanar UV mapping
- **Full editor environment** — hierarchy panel, inspector panel, asset browser, project settings, console
- **Transform gizmos** — Move, Rotate, and Scale with multi-axis handles
- **Entity Component System** — parallel-array architecture with 8 component types
- **Physics system** — gravity, OBB/Sphere/Capsule/Pyramid collision with SAT, rigid body angular dynamics
- **Player controller** — 4 movement modes, local and channel-based input bindings
- **Prefab system** — create, instantiate, edit, and sync reusable entity templates
- **OBJ import** — drag-and-drop mesh import with automatic AABB computation
- **Scene serialization** — full round-trip save/load in plain-text format
- **Multi-select** — Ctrl+Click and Shift+Click entity selection in the hierarchy
- **Input channel system** — runtime-configurable input bindings without recompilation

---

## Technical Stack

| Layer | Technology |
|---|---|
| Language | C++17 |
| Build | CMake 3.16+ |
| Graphics API | OpenGL 4.6 Core Profile |
| GL Loader | GLAD |
| Window / Input | GLFW |
| Editor UI | Dear ImGui v1.91.6 |
| Math | GLM |
| Mesh Import | tinyobjloader v1.0.6 |
| Image Loading | stb_image |

---

## Engine Conventions

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
├── CMakeLists.txt
├── assets/
│   └── scenes/              saved .tscene files
├── engine/
│   ├── components/          component struct headers
│   ├── core/                Application loop, Window
│   ├── editor/              EditorCamera, EditorGizmo
│   ├── input/               InputManager
│   ├── physics/             PhysicsSystem
│   ├── renderer/            Renderer, Mesh, TextureLoader
│   ├── scene/               Scene, Entity
│   ├── serialization/       SceneSerializer, PrefabSerializer
│   └── ui/                  HierarchyPanel, InspectorPanel, UIManager
├── external/                GLAD, GLFW, GLM, stb
├── sandbox/
│   └── main.cpp             entry point (WinMain on Windows)
└── shaders/                 legacy reference shaders (unused)
```

---

## Scene and Entity System

All scene data lives in **parallel arrays**. Every entity is an integer index. A component is present when its `Active` flag is `true`.

```
Index 0  →  Transforms[0], MeshRenderers[0], RigidBodies[0], Lights[0], ...
Index 1  →  Transforms[1], MeshRenderers[1], RigidBodies[1], Lights[1], ...
```

### Per-Entity Arrays

| Array | Purpose |
|---|---|
| `Transforms` | Local TRS (position, rotation, scale, parent) |
| `MeshRenderers` | Mesh pointer and active flag |
| `RigidBodies` | Physics: gravity, collider, rigid body modules |
| `GameCameras` | In-game perspective cameras |
| `PlayerControllers` | Character movement controllers |
| `MouseLooks` | Standalone mouse-look rotation |
| `Lights` | Directional, Point, Spot, Area light data |
| `EntityNames` | Display names |
| `EntityParents` / `EntityChildren` | Parent-child hierarchy |
| `EntityGroups` | Scene group membership |
| `EntityMaterial` | Material asset index (-1 = none) |
| `EntityColors` | Per-entity color override |
| `EntityPrefabSource` | Prefab instance tracking (-1 = none) |

### Key Scene Methods

| Method | Description |
|---|---|
| `GetEntityWorldMatrix(i)` | Walks the parent chain and returns the full world TRS matrix |
| `GetEntityWorldPos(i)` | World position extracted from the world matrix |
| `GetActiveGameCamera()` | Index of the first active GameCameraComponent, or -1 |
| `SetEntityParent(child, parent)` | Reparent an entity (-1 = unparent) |
| `DeleteEntity(idx)` | Remove entity and fix all references |
| `CreateGroup(name, parent)` | Create a hierarchy folder |

### Scene Groups

Groups are hierarchy folders with their own `TransformComponent`. Child entities and sub-groups inherit the group's world transform. Groups can be nested arbitrarily.

### Scene Assets

The scene owns all asset data:

| Asset Type | Description |
|---|---|
| `Materials` | PBR materials with albedo/normal/ORM textures and scalar fallbacks |
| `Textures` | Imported image files with per-texture import settings |
| `Prefabs` | Reusable entity templates (.tprefab files) |
| `MeshAssets` | Registered OBJ file paths |
| `Channels` | Named variables (Boolean, Float, String) for runtime input configuration |
| `Folders` | Virtual folder hierarchy for asset organization |

---

## Components

### TransformComponent

| Field | Type | Default | Description |
|---|---|---|---|
| `Position` | vec3 | {0,0,0} | Local position |
| `Rotation` | vec3 | {0,0,0} | Euler rotation in degrees (X=roll, Y=yaw, Z=pitch) |
| `Scale` | vec3 | {1,1,1} | Local scale |
| `Parent` | int | -1 | Parent entity index (-1 = no parent) |

`GetMatrix()` returns the local TRS matrix. The full world matrix is computed by `Scene::GetEntityWorldMatrix()` via recursive parent chain traversal.

### MeshRendererComponent

| Field | Type | Description |
|---|---|---|
| `MeshPtr` | Mesh* | Pointer to a mesh (Box, Sphere, Capsule, Cylinder, Pyramid, Plane, or OBJ) |
| `Active` | bool | Whether the mesh is rendered this frame |

### GameCameraComponent

Perspective camera for the in-game view. The camera looks along its world **-Z** axis.

| Field | Default | Description |
|---|---|---|
| `FOV` | 75 | Vertical field of view in degrees |
| `Near` / `Far` | 0.1 / 500 | Clipping planes |
| `Active` | false | Only the first active camera is used |
| `Yaw` / `Pitch` | -90 / 0 | Camera orientation angles |

### RigidBodyComponent

Three independently toggleable sub-modules:

**Gravity Module**

| Field | Default | Description |
|---|---|---|
| `UseGravity` | true | Apply downward acceleration |
| `IsKinematic` | false | Move by code only, not forces |
| `Mass` | 1.0 | Kilograms |
| `Drag` | 0.01 | Linear velocity damping per tick |
| `FallSpeedMultiplier` | 1.0 | Gravity acceleration scale |
| `Velocity` | {0,0,0} | World-space velocity (m/s) |
| `IsGrounded` | false | True when resting on a surface |

**Collider Module**

| Field | Default | Description |
|---|---|---|
| `Collider` | Box | Shape: Box, Sphere, Capsule, Pyramid, or None |
| `ColliderSize` | {1,1,1} | Full extents for Box |
| `ColliderRadius` | 0.5 | Radius for Sphere / Capsule |
| `ColliderHeight` | 1.0 | Height for Capsule |
| `ColliderOffset` | {0,0,0} | Local offset from entity origin |
| `ShowCollider` | false | Renders green wireframe in editor |

**RigidBody Mode**

| Field | Default | Description |
|---|---|---|
| `AngularVelocity` | {0,0,0} | Rotational velocity (degrees/sec) |
| `AngularDamping` | 0.0 | 0 = no damping, 1 = instant stop |
| `Restitution` | 0.25 | Bounce coefficient (0–1) |
| `FrictionCoef` | 0.15 | Lateral friction on impact |

### PlayerControllerComponent

Character controller with 4 movement modes and 2 input modes.

**Movement Modes:**

| Mode | Description |
|---|---|
| WorldWithCollision | World-space X/Z axes, physics collision |
| **LocalWithCollision** | Entity-relative facing direction, physics collision **(default)** |
| WorldNoCollision | World-space, noclip |
| LocalNoCollision | Facing-relative, noclip |

**Input Modes:**

| Mode | Description |
|---|---|
| Local | Reads GLFW key codes directly (default: WASD + Shift/Ctrl) |
| Channels | Maps actions to channel indices with string-based key names |

| Field | Default | Description |
|---|---|---|
| `WalkSpeed` | 4.0 | Base movement speed (units/sec) |
| `RunMultiplier` | 1.8 | Speed multiplier while running |
| `CrouchMultiplier` | 0.45 | Speed multiplier while crouching |

### MouseLookComponent

Standalone mouse-look rotation. Controls yaw and pitch of separate target entities.

| Field | Description |
|---|---|
| `YawTargetEntity` | Entity rotated around world Y |
| `PitchTargetEntity` | Entity rotated around local X |
| `SensitivityX` / `SensitivityY` | Per-axis sensitivity |
| `InvertX` / `InvertY` | Axis inversion |
| `ClampPitch` | Enable pitch clamping |
| `PitchMin` / `PitchMax` | Clamp range (recommend -89 to 89) |

### LightComponent

| Type | Description |
|---|---|
| Directional | Sun-like infinite light. Direction from entity rotation. Casts 2D shadow. |
| Point | Omnidirectional. Attenuates with distance. Casts cubemap shadow. |
| Spot | Cone with inner/outer angle falloff. Casts 2D shadow. |
| Area | Rectangular source with Width/Height. |

| Field | Default | Description |
|---|---|---|
| `Color` | {1,1,1} | RGB multiplier |
| `Temperature` | 6500 | Color temperature in Kelvin |
| `Intensity` | 1.0 | Brightness scalar |
| `Range` | 10.0 | Attenuation distance (Point, Spot, Area) |
| `InnerAngle` | 30 | Spot inner cone half-angle (degrees) |
| `OuterAngle` | 45 | Spot outer cone half-angle (degrees) |
| `Width` / `Height` | 1.0 | Area light dimensions |

Up to **8 active lights** per frame.

---

## Renderer

Static class. Handles all rendering through a forward PBR pipeline.

### Per-Frame Pipeline

1. **Cache World Matrices** — builds all entity world matrices once per frame to avoid redundant recursive parent-chain traversal across render passes
2. **Shadow Pass** (`RenderShadowPass`) — renders depth from the first active directional/spot light (2D shadow map) and the first active point light (cubemap shadow)
3. **Scene Pass** (`DrawScene`) — forward-renders all mesh entities with PBR shading, up to 8 lights, and shadow lookup
4. **Gizmos** (editor only) — collider wireframes, game camera visualization

### PBR Shading

The fragment shader implements a full Cook-Torrance BRDF:

- **Distribution**: GGX (Trowbridge-Reitz)
- **Geometry**: Smith's method with Schlick-GGX approximation
- **Fresnel**: Schlick approximation with roughness-aware variant for ambient
- **Textures**: Albedo (unit 0), Normal map (unit 2), ORM packed texture (unit 3)
- **UV Mapping**: Triplanar projection (world-space or object-space), configurable tiling
- **Tone Mapping**: Reinhard
- **Gamma Correction**: sRGB (gamma 2.2)
- **Ambient**: Fresnel-weighted diffuse ambient with AO, floor of 0.08
- **Fallback**: soft directional light when no lights exist in the scene

### Shadow System

**2D Shadow Map (Directional / Spot)**

| Property | Value |
|---|---|
| Resolution | 1024 × 1024 |
| Filter | PCF 3×3 kernel |
| Bias | Adaptive: max(0.005 × (1 - N·L), 0.0005) |
| Max shadow | 70%. Minimum 30% light always passes. |
| Outside map | No shadow (border clamp = 1.0) |
| Directional | Orthographic ±30 units, eye at -forward × 40 |
| Spot | Perspective, FOV = OuterAngle × 2 |

**Point Light Cubemap Shadow**

| Property | Value |
|---|---|
| Resolution | 512 × 512 per face |
| Faces | All 6 rendered in single pass via geometry shader |
| Filter | 4-sample PCF with offset directions |
| Bias | Distance-dependent: 0.15 + 0.05 × (depth / far) |
| Max shadow | 80%. Minimum 20% light always passes. |
| Far plane | 25.0 units |
| Self-shadow prevention | Front-face culling during shadow pass |

**Per-Light Shadow**: Shadow attenuation is applied only to the specific light that cast it, not globally. Other lights illuminate shadow regions normally.

### Temperature to RGB

Kelvin values (1000–12000) are converted to approximate RGB using a piecewise formula and multiplied into the light color before upload.

### Performance Optimizations

- **VSync enabled** via `glfwSwapInterval(1)`
- **Uniform location caching** — all `glGetUniformLocation` calls happen once at init, stored in a `ShaderLocs` struct
- **Per-frame world matrix cache** — `CacheWorldMatrices()` builds a flat vector of all entity world matrices once, eliminating redundant recursive parent-chain traversal across shadow pass, point shadow pass, and draw pass

### Public API

| Method | Description |
|---|---|
| `Init()` | Compile PBR/gizmo/shadow/HUD shaders, build FBOs, cache uniform locations |
| `BeginFrame()` | Clear framebuffer |
| `RenderSceneEditor(scene, cam, w, h)` | Full editor render: cache matrices → shadow → scene → gizmos |
| `RenderSceneGame(scene, w, h)` | Game render from active game camera |
| `RenderToolbar(playing, paused, w, h)` | Play/Pause HUD overlay |
| `DrawTranslationGizmo(pos, axis, cam, w, h)` | 3-axis translation gizmo (X, Y, Z + XY, XZ, YZ planes) |
| `DrawRotationGizmo(pos, axis, cam, w, h)` | 3-ring rotation gizmo |
| `DrawScaleGizmo(pos, axis, cam, w, h)` | 3-axis scale gizmo with cube tips |

---

## Mesh System

### Primitive Types

| Type | Description |
|---|---|
| Cube | 12 triangles, face normals |
| Sphere | UV sphere (16 stacks × 32 slices) |
| Capsule | Cylinder body + hemispherical caps |
| Cylinder | 32-segment parametric with top/bottom caps |
| Pyramid | 4 lateral + 2 base triangles |
| Plane | Double-sided 1×1 XZ quad |

### OBJ Import

`Mesh::LoadOBJ(path)` loads arbitrary OBJ files via tinyobjloader. Vertices are auto-normalized to a 1-unit bounding box. AABB (`BoundsMin`, `BoundsMax`) is computed from vertex data for raycast picking.

### Vertex Format

9 floats per vertex: position(3) + normal(3) + color(3). All meshes use `GL_TRIANGLES`.

---

## Texture System

| Function | Description |
|---|---|
| `LoadTexture(path)` | sRGB load for albedo textures, vertical flip, mipmaps |
| `LoadTextureLinear(path)` | Linear load for normal/data maps, no flip |
| `PackORM(ao, rough, metal, defaults)` | Packs 3 greyscale textures into a single RGB ORM texture (R=AO, G=Roughness, B=Metallic) with nearest-neighbour resampling |

### Texture Import Settings

| Setting | Options |
|---|---|
| `IsLinear` | sRGB (albedo) or Linear (normal/data) |
| `WrapS` / `WrapT` | Repeat, Clamp, Mirrored Repeat |
| `Filter` | Linear + Mipmaps, Nearest |
| `Anisotropy` | 1–16 |

---

## PBR Material System

Materials are stored as `MaterialAsset` in the scene and support full PBR workflows.

| Property | Description |
|---|---|
| `Color` | RGB tint multiplied over albedo |
| `AlbedoPath` | Albedo texture path |
| `NormalPath` | Normal map path |
| `AOPath` / `RoughnessPath` / `MetallicPath` | Source textures packed into ORM at runtime |
| `Roughness` | Scalar fallback (0.0–1.0, default 0.5) |
| `Metallic` | Scalar fallback (0.0–1.0, default 0.0) |
| `AOValue` | Scalar fallback (0.0–1.0, default 1.0) |
| `Tiling` | UV scale (x = XZ, y = Y) |
| `WorldSpaceUV` | Triplanar mapping using world position (seamless across objects) |

Materials are assigned per-entity and can be dragged from the Asset Browser onto entities in the hierarchy or inspector.

---

## Physics System

Per-tick simulation over all entities with active RigidBodyComponent modules.

**Gravity**: 9.8 m/s² × FallSpeedMultiplier downward. Drag applied as `Velocity *= (1 - Drag)`.

### Collision Detection

Full collision pair matrix using proper 3D algorithms:

| Pair | Method |
|---|---|
| OBB vs OBB | SAT with 15 separating axes |
| Sphere vs OBB | Closest-point on OBB |
| Sphere vs Sphere | Center-to-center distance |
| Capsule vs OBB | Iterative closest-point |
| Capsule vs Sphere | Segment-to-sphere distance |
| Capsule vs Capsule | Segment-to-segment closest points |
| Pyramid vs OBB | SAT with pyramid face normals |
| Pyramid vs Sphere | Closest point on triangle faces |
| Pyramid vs Capsule | Triangle face intersection |
| Pyramid vs Pyramid | SAT between two pyramid meshes |

**OBB Construction**: Built from world matrix, fully supports rotation and non-uniform scale.

**Collision Response**: Normal-based depenetration, velocity reflection by restitution, lateral friction. `IsGrounded` set when resting on a surface.

**RigidBody Mode**: Angular velocity accumulation from impacts, configurable angular damping.

---

## Input System

| Method | Description |
|---|---|
| `IsKeyPressed(code)` | True while key is held |
| `IsKeyJustPressed(code)` | True on first frame of key-down transition |
| `GetMouseDelta()` | dx/dy since last frame |
| `KeyNameToCode(name)` | "W", "Space", "Left Shift" → GLFW key code |
| `KeyCodeToName(code)` | GLFW key code → human-readable name |

### Input Channels

Named variables in `Scene::Channels` (Boolean, Float, or String). In Channels input mode, the PlayerController maps each action to a channel index whose `StringValue` holds the key name. Channels are edited in the Project Settings panel and allow runtime input rebinding without recompilation.

---

## Editor

### Application and Main Loop

Engine modes: **Editor**, **Game**, **Paused**.

**Play system**: F1 = Play/Stop, F2 = Pause/Resume (also via toolbar buttons). Editor state (positions, rotations, velocities) is snapshot before play and restored on stop.

**Editor input**: Ray-AABB picking for entity selection, gizmo interaction (Move/Rotate/Scale with drag detection), viewport right-click context menu (create entities, lights, cameras), gizmo hotkeys (W=Move, E=Scale, R=Rotate).

### Editor Camera

Free-fly camera with right-click look, WASD movement, middle-mouse pan, scroll zoom.

| Feature | Details |
|---|---|
| Movement | WASD + Space/E (up) + Ctrl/Q (down) |
| Look | Right-mouse drag |
| Pan | Middle-mouse drag |
| Zoom | Scroll wheel |
| Speed boost | Hold Shift (3× multiplier) |
| Scroll speed | Shift+Scroll adjusts (0.01–10.0 range, shows HUD) |
| Projection | Perspective, FOV=60°, Near=0.1, Far=1000 |

### Editor Gizmos

Three gizmo modes with constant screen-size rendering (15% of camera distance):

| Mode | Features |
|---|---|
| **Move** | 3 single-axis arrows + 3 dual-axis plane handles (XY, XZ, YZ) |
| **Rotate** | 3 ring circles (64 segments per ring, 18px hit radius) |
| **Scale** | 3 axes with cube tips + 3 plane handles |

### Hierarchy Panel

Left panel (260px default, resizable) displaying the full entity and group tree.

**Features:**
- Click to select, drag to reorder or reparent
- Multi-select: Ctrl+Click (toggle) and Shift+Click (range)
- Inline rename for entities, groups, and prefab nodes
- Right-click context menu: Create 3D Objects (Cube, Sphere, Plane, Pyramid, Cylinder, Capsule), Create Lights (Directional, Point, Spot, Area), Create Camera, Create Group, Rename, Unparent, Unpack Prefab, Delete
- Drag-drop zones between siblings for reorder, onto groups for reparenting, onto bottom zone for unparent
- Material drag-drop onto entities from Asset Browser
- Prefab instances shown in **blue**; orphaned instances in **red**
- Component type icons next to entity names

**Prefab Editor Mode**: When editing a prefab, the hierarchy shows the prefab's node tree with right-click menus for adding child nodes (3D Objects, Empty, Light, Camera), renaming, and deleting nodes. Includes Sync Instances and Save to Disk buttons.

### Inspector Panel

Right panel (260px default, resizable) showing all components on the selected entity with collapsible sections.

| Section | Key Features |
|---|---|
| **Transform** | Position, Rotation (degrees), Scale — colored X/Y/Z labels, double-right-click reset |
| **Material** | Color picker, material dropdown, drag-drop assignment |
| **Game Camera** | FOV, Near, Far, Yaw, Pitch, enable toggle |
| **Gravity** | UseGravity, IsKinematic, Mass, Drag, FallSpeedMultiplier, Velocity |
| **Collider** | Type dropdown, Size/Radius/Height/Offset, ShowCollider toggle |
| **RigidBody** | AngularVelocity, AngularDamping, Restitution, Friction |
| **Player Controller** | Movement mode, input mode, key bindings, speeds |
| **Mouse Look** | Target entities, sensitivity, invert, pitch clamping |
| **Light** | Type, Color, Temperature, Intensity, Range, Angles, Width/Height |
| **Add Component** | Searchable popup to add any component |

Components can be reordered via drag-drop on headers and removed via right-click context menu.

### Asset Browser

Bottom panel (200px) organized in a virtual folder hierarchy.

**Asset Types**: Folders, Materials, Textures, Prefabs, OBJ Mesh Assets

**Features:**
- Folder navigation with breadcrumb bar
- External file drag-drop: OBJ files → mesh assets, images → textures
- Prefab creation: drag entity from hierarchy into the drop zone
- Prefab instantiation: right-click → Instantiate, or drag to viewport
- Context menus: rename, delete, move to folder on all asset types
- Background context menu: create new folders and materials
- Double-click prefab → opens Prefab Editor tab
- Double-click OBJ → instantiates new entity

### Viewport Tabs

| Tab | Description |
|---|---|
| Editor Camera | Main 3D viewport with editor camera |
| Game Camera | In-game camera preview |
| Project Settings | Channel system configuration (closeable) |
| Prefab Editor | Edit prefab nodes with 3D preview (closeable) |

### Console

Log panel (tab next to Assets) with max 200 entries (deque). Accessible via `UIManager::Log()`.

### Splash Screen

Animated smiley-face logo drawn via ImDrawList for 3.2 seconds on startup. The same logo is used as the procedurally generated window icon (64×64) and in the menu bar.

---

## Prefab System

### Creating Prefabs

Drag an entity (or entity tree) from the Hierarchy into the Asset Browser drop zone. The system recursively snapshots transforms, meshes, materials, lights, and cameras into a `PrefabAsset` and saves it as a `.tprefab` file.

### Instantiating Prefabs

Right-click a prefab → Instantiate, or drag from Asset Browser into the viewport. Creates entities for all nodes with proper parent-child relationships.

### Prefab Instance Tracking

Entities track their source prefab via `EntityPrefabSource`. This enables:
- **Orphaned detection**: deleted prefab sources → instances turn red in hierarchy
- **Unpack Prefab**: right-click to detach from source, making it standalone

### Prefab Editor

Double-click a prefab to open a dedicated editor tab with:
- Editable prefab name and per-node properties (name, transform, mesh type, material, light, camera)
- Right-click context menus: Add Child (3D Objects, Empty, Light, Camera), Rename, Delete Node
- **Sync Instances** button: propagate edits to all scene instances
- **Save to Disk** button: write prefab back to `.tprefab` file
- Full 3D preview with gizmo support

---

## Scene Serialization

Scenes are saved as plain-text `.tscene` files. Prefabs use `.tprefab` files.

### Scene File Format

```
[material]
name = Metal
color = 0.8 0.8 0.8
roughness = 0.3
metallic = 0.9

[channel]
ch_idx = 0
ch_name = Forward
ch_type = string
ch_string = W

[entity]
name = Player
pos  = 0.0 1.0 0.0
rot  = 0.0 0.0 0.0
scale = 1.0 1.0 1.0
mesh = capsule
player_controller = true
pc_move_mode = 2
```

### Prefab File Format

```
name = MyPrefab
[node]
node_idx = 0
node_name = Root
node_parent = -1
node_mesh = cube
node_pos = 0.0 0.0 0.0
node_rot = 0.0 0.0 0.0
node_scale = 1.0 1.0 1.0
```

Supports full round-trip save/load of all entity data including PBR materials, physics modules, player controllers, mouse look, lights, and channel bindings.

---

## OBJ Import

OBJ files can be dragged from the file system into the editor.

| Drop Target | Behavior |
|---|---|
| Viewport | Creates entity with OBJ mesh at origin |
| Asset Browser | Creates entity AND registers OBJ as a mesh asset with "OBJ" icon |

Registered mesh assets appear in the Asset Browser with support for double-click instantiation, right-click deletion, and drag-drop.

`Mesh::LoadOBJ()` auto-normalizes meshes to a 1-unit bounding box and computes AABB for accurate raycast picking.

---

## Building

**Requirements:**
- CMake 3.16 or higher
- C++17 compiler (MSVC recommended on Windows)
- OpenGL 4.6 capable GPU

```bash
cmake -S . -B build
cmake --build build --config Release
./build/Release/TsuEngine.exe
```

GLFW is included under `external/glfw/`. GLAD, GLM, and stb are header-only in `external/`. ImGui and tinyobjloader are fetched automatically via CMake FetchContent.

---

## Default Scene

The default scene (`assets/scenes/default.tscene`) contains:
- A camera entity
- A white cube at the origin
- An orange pyramid
- A blue cylinder
- A ground plane (flattened cube)

---

## License

No license applied yet. All rights reserved.
