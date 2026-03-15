<div align="center">
  <img src="docs/images/logo.svg" width="100" alt="TSU Engine Logo"/>
  <h1>TSU Engine</h1>

  [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
  [![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
  [![OpenGL 4.6](https://img.shields.io/badge/OpenGL-4.6-red.svg)](https://www.opengl.org/)
  [![Status: Alpha](https://img.shields.io/badge/Status-Alpha%201.3.1-orange.svg)]()
  [![Platform: Windows](https://img.shields.io/badge/Platform-Windows-lightgrey.svg)]()
</div>

> ⚠ **ALPHA — This project is under active development.** Many systems are incomplete, untested, or subject to change. See the [alpha notice](#alpha-notice) below.

TSU Engine is a custom 3D game engine built from scratch in C++17, using OpenGL 4.6 as its rendering backend. It features a fully integrated editor environment, a forward PBR rendering pipeline, a Lua 5.4 scripting system, a physics simulation, a prefab system, procedural room generation, and a standalone game export workflow — all written without any large third-party engine frameworks.

---

## Alpha Notice

**This is an early alpha release. The engine is still in active development, has known bugs, and is not production-ready. Systems such as the Maze Generator are present in the codebase but are not fully functional in this version. Expect breaking changes between versions.**

---

## Overview

TSU Engine was created to support the development of a specific game concept centered around procedural, modular labyrinth environments. The engine provides full architectural control over the rendering pipeline, scene structure, and runtime systems rather than relying on pre-built abstractions.

The project has since evolved into a structured general-purpose 3D engine with:

- A complete in-editor workflow (create, configure, export)
- A lean standalone **GameRuntime** binary that contains only what the exported game needs
- Planned full Linux support — both for running the editor and for exporting games to Linux targets

The codebase is organized to be readable, extensible, and contributor-friendly.

> The project is currently open source during early development. Future versions may expand or restructure the engine and may change distribution models.

---

## Features

### Rendering
- Forward PBR pipeline — Cook-Torrance BRDF (GGX distribution, Smith geometry, Schlick Fresnel)
- Multi-light system — up to 8 simultaneous lights: Directional, Point, Spot, Area
- Dual shadow mapping — PCF-filtered 2D shadow maps + cubemap shadows for point lights
- PBR material system — Albedo, Normal, and packed ORM (AO / Roughness / Metallic) textures with triplanar UV mapping
- Post-processing pipeline — GL_RGBA16F FBO pass with bloom, vignette, film grain, chromatic aberration, and full color grading
- Procedural gradient sky — zenith/horizon/ground gradient with a configurable sun disc
- Atmospheric fog — Linear, Exponential, and Exp modes
- Frustum culling — Gribb-Hartmann plane extraction, bounding-sphere test per entity

### Editor
- Full editor environment — Hierarchy, Inspector, Asset Browser, Console
- Transform gizmos — Move, Rotate, and Scale with multi-axis handles
- Viewport tabs — Editor Camera, Game Camera, Project Settings, Prefab Editor, Room Editor
- Prefab system — create, edit, instantiate, and sync reusable entity templates
- Project management — New Project, Open Project, Save Project, Export Game

### Gameplay Systems
- Entity Component System — parallel-array architecture
- Physics system — gravity, OBB/Sphere/Capsule/Pyramid collision with SAT, rigid body angular dynamics
- Player controller — 4 movement modes (world/local, collision/noclip), headbob
- Mouse look — standalone first-person look component
- Trigger volumes — AABB overlap detection firing boolean channels
- Keyframe animator — Position / Rotation / Scale interpolation with multiple modes and easings
- Input channel system — runtime-configurable key bindings without recompilation

### Scripting
- Lua 5.4 scripting — per-entity scripts with `OnStart` / `OnUpdate` / `OnStop` lifecycle
- Scene API — full Lua bindings for reading and writing entity transforms, channels, and post-processing
- `--@expose` annotations — expose script variables to the editor inspector with automatic type detection

### Asset & Scene Pipeline
- OBJ import — drag-and-drop with automatic AABB computation and mesh normalization
- Scene serialization — complete round-trip save/load in human-readable plain-text `.tscene` format
- Prefab serialization — `.tprefab` files with full node hierarchy

### Multiplayer
- Raw UDP networking — host/client architecture over Winsock sockets
- `MultiplayerManager` component — configurable mode (host/client), port, max clients, snapshot rate, auto-start
- `MultiplayerController` component — per-entity network ID, nickname, local/remote flag, transform sync
- Prefab-based player spawning — `mm_player_prefab` field spawns player slots at runtime from an inline prefab
- Snapshot replication — authoritative transform snapshots broadcast to all clients every tick
- Launch args — `--mp-mode=host|client`, `--mp-server=IP`, `--mp-port=PORT`, `--mp-nick=NAME`
- Lua multiplayer API — `scene.isConnected()`, `scene.isHost()`, `scene.playerCount()`, `scene.getNetworkId(idx)`, `scene.setNickname(idx, name)`

### Export
- Standalone game export — copies `GameRuntime.exe` (editor-free lean binary) + assets to an output folder
- `game.mode` marker — runtime detection of standalone mode, automatic fullscreen, Lua autostart

---

## Installation

**Requirements:**
- CMake 3.16+
- C++17 compiler (MSVC recommended on Windows)
- OpenGL 4.6 capable GPU

```bash
git clone https://github.com/tsuyu122/tsu-engine
cd tsuEngine
cmake -S . -B build
cmake --build build --config Release
```

> GLFW is included under `external/glfw/`. GLAD, GLM, and stb are header-only in `external/`. ImGui and tinyobjloader are downloaded automatically via CMake FetchContent.

---

## Project Structure

```
tsuEngine/
 CMakeLists.txt             Build configuration (TsuEngine + GameRuntime targets)
 LICENSE                    MIT License
 README.md

 assets/
    scenes/                Scene files (.tscene)
    scripts/               Lua scripts

 docs/                      Project documentation
    index.md               Documentation home
    lua/                   Lua scripting documentation

 engine/
    components/            Component struct headers (pure data, no logic)
    core/                  Application loop, Window, GameApplication (lean runtime)
    editor/                EditorCamera, EditorGizmo
    input/                 InputManager
    lua/                   LuaSystem (Lua 5.4 bindings and lifecycle)
    physics/               PhysicsSystem
    procedural/            MazeGenerator
    renderer/              Renderer, Mesh, TextureLoader, LightmapManager
    scene/                 Scene, Entity
    serialization/         SceneSerializer, PrefabSerializer
    ui/                    HierarchyPanel, InspectorPanel, UIManager (editor only)

 external/                  Vendored dependencies (GLAD, GLFW, GLM, stb)

 sandbox/
    main.cpp               Editor entry point
    game_main.cpp          GameRuntime entry point (no editor)

 shaders/                   Legacy reference shaders (currently unused)
```

---

## Usage

### Running the Editor

```
build/Release/TsuEngine.exe
```

The editor opens with the default scene. Use the Hierarchy to create entities, the Inspector to configure components, and the **Play** button (or `F1`) to enter game mode.

### Exporting a Game

1. Build your scene in the editor
2. Open **File → Export Game...**
3. Enter the output folder path and confirm
4. The exporter copies `GameRuntime.exe` (lean player, no editor code) and all assets
5. Distribute the output folder  the `.exe` is self-contained

### Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `F1` | Play / Stop |
| `F2` | Pause / Resume |
| `W` | Move gizmo |
| `E` | Scale gizmo |
| `R` | Rotate gizmo |
| `Ctrl+S` | Save Project |
| `Escape` | Quit (in exported game) |

---

## Lua Scripting System

TSU Engine supports per-entity Lua 5.4 scripts for extending gameplay behavior without recompiling.

### Attaching a Script

1. Select an entity in the hierarchy
2. In the Inspector, find the **Lua Script** section
3. Drag a `.lua` file from the Asset Browser onto the script slot

### Script Lifecycle

```lua
function OnStart()
    -- Called once when Play begins
end

function OnUpdate(dt)
    -- Called every frame. dt = delta time in seconds.
end

function OnStop()
    -- Called when Play ends
end
```

### Exposing Variables to the Inspector

```lua
--@expose speed number 5.0
--@expose enabled boolean true
--@expose label string "hello"

function OnUpdate(dt)
    if not enabled then return end
    local x, y, z = scene.getPos(entity_idx)
    scene.setPos(entity_idx, x + speed * dt, y, z)
end
```

For the full API reference see [`docs/lua/api.md`](docs/lua/api.md).

---

## Architecture Notes

### Parallel-Array ECS

```
Index 0    Transforms[0], MeshRenderers[0], RigidBodies[0], Lights[0], ...
Index 1    Transforms[1], MeshRenderers[1], RigidBodies[1], Lights[1], ...
```

Components are toggled by their `Active` flag, not removed from the array. All systems iterate contiguous arrays with no pointer chasing.

### Two Binaries

| Binary | Contents |
|---|---|
| `TsuEngine.exe` | Full editor (ImGui, gizmos, panels, UIManager) |
| `GameRuntime.exe` | Lean player — rendering, physics, Lua, input only. Zero ImGui. |

`ExportGame` copies `GameRuntime.exe`, not the editor.

### Engine Coordinate Conventions

| Axis | Role |
|---|---|
| **+X** | Entity local forward |
| **+Y** | World and local up |
| **+Z** | Entity local right |

`Rotation.x` = Pitch, `Rotation.y` = Yaw, `Rotation.z` = Roll (stored in degrees).

---

## Roadmap

- [ ] **Full Linux support** — run the editor on Linux, export games for Linux targets
- [ ] Complete Maze Generator system
- [ ] Vulkan render backend
- [ ] Audio system (3D positional audio)
- [ ] Skeletal animation and blend trees
- [ ] HUD / in-game UI system (screen-space panels, health bars, crosshairs)
- [ ] Plugin / native module system
- [ ] Asset bundling — packed binary archives for distribution
- [ ] Improved lightmap baking pipeline
- [x] Network replication — basic authoritative multiplayer (host/client, UDP, prefab player spawning) ✓ v1.3.1
- [ ] Cross-platform export (Windows, Linux, macOS)

---

## Contributing

Contributions, bug reports, and feature suggestions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on submitting issues, pull requests, and code style conventions.

---

## License

TSU Engine is released under the [MIT License](LICENSE).  
Copyright (c) 2026 VHM
