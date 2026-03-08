# TSU Engine — Documentation

Welcome to the TSU Engine documentation.

TSU Engine is a custom 3D game engine written in C++17 with an OpenGL 4.6 rendering backend. It includes a fully integrated editor, a forward PBR pipeline, a Lua 5.4 scripting system, a physics engine, a prefab workflow, and a standalone game export pipeline.

---

## Contents

| Document | Description |
|---|---|
| [Lua Overview](lua/overview.md) | How the Lua scripting system integrates with the engine |
| [Lua API Reference](lua/api.md) | All built-in Lua functions exposed by the engine |
| [Lua Examples](lua/examples.md) | Practical script examples for common game behaviors |

---

## Engine Architecture Summary

The engine is split into two compiled targets:

- **TsuEngine** — the full editor binary with ImGui, gizmos, and all UI panels
- **GameRuntime** — the lean player binary with zero editor code; used for shipped games

Both share the same core systems: Renderer, PhysicsSystem, LuaSystem, InputManager, Scene, and MazeGenerator.

### System Map

```
Application (editor)
├── Window
├── Renderer          ← PBR forward pipeline, shadows, post-processing
├── Scene             ← Parallel-array ECS: Transforms, MeshRenderers, Physics, ...
├── PhysicsSystem     ← Gravity, collision detection (SAT, OBB, Sphere, Capsule, Pyramid)
├── LuaSystem         ← Lua 5.4 per-entity scripts; OnStart / OnUpdate / OnStop
├── InputManager      ← GLFW key/mouse abstraction, scroll, channel key-name parsing
├── MazeGenerator     ← Procedural modular room spawning (work in progress)
├── EditorCamera
├── EditorGizmo
└── UIManager         ← Dear ImGui panels (editor only)

GameApplication (runtime)
├── Window
├── Renderer
├── Scene
├── PhysicsSystem
├── LuaSystem
├── InputManager
└── MazeGenerator
```

---

## Building

```bash
git clone https://github.com/tsuyu122/tsu-engine
cd tsuEngine
cmake -S . -B build
cmake --build build --config Release
```

Produces:
- `build/Release/TsuEngine.exe` — the editor
- `build/Release/GameRuntime.exe` — the lean game player

---

## Scene Format

Scenes are saved as plain-text `.tscene` files in `assets/scenes/`. They are human-readable and can be version-controlled. Prefabs use `.tprefab` files.

---

## Component Reference

Every entity is an integer index shared across all component arrays. A component is active when its `Active` flag is `true`.

| Array | Purpose |
|---|---|
| `Transforms` | Local position, rotation (degrees), scale, parent index |
| `MeshRenderers` | Mesh pointer (primitives or OBJ) |
| `RigidBodies` | Gravity, collider, and rigid-body sub-modules |
| `GameCameras` | In-game perspective cameras |
| `PlayerControllers` | Character movement (WASD, run, crouch, headbob) |
| `MouseLooks` | First-person mouse-look rotation |
| `Lights` | Directional, Point, Spot, Area — up to 8 active per frame |
| `Triggers` | AABB overlap volumes that fire input channels |
| `Animators` | Keyframe animators for Position / Rotation / Scale |
| `MazeGenerators` | Procedural maze controller *(incomplete in alpha)* |
| `LuaScripts` | Attached `.lua` script path with exposed variable map |

---

## Coordinate System

| Axis | Role |
|---|---|
| **+X** | Entity local forward |
| **+Y** | World and local up |
| **+Z** | Entity local right |

- `Rotation.x` = Pitch
- `Rotation.y` = Yaw
- `Rotation.z` = Roll
- All rotation values are stored in **degrees**

---

## License

TSU Engine is released under the [MIT License](../LICENSE).  
Copyright (c) 2026 VHM
