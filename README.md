Tsu Game Engine

Tsu Game Engine is a 3D engine written in C++ that originated from the need to build a very specific game concept.

The project did not begin as a learning exercise. It started because I wanted to create a particular type of game that required a level of architectural control, modularity, and lighting behavior that I could not realistically achieve using existing game engines. Building a custom engine became the only practical solution.

As development progressed, the project naturally evolved into a deeper exploration of engine architecture. What began as a technical necessity gradually became both a study of how modern engines work internally and a decision to intentionally grow it into a fully structured game engine.

Vision
Tsu is being built as a modular and highly controlled 3D engine with its own editor environment. The goal is to maintain full authority over the rendering pipeline, scene structure, and system design instead of relying on pre-built abstractions.

The engine is specifically focused on generating high graphical quality experiences built around procedural, module-based labyrinth generation. The core idea is to construct environments from predefined modular pieces and, in the future, precalculate all possible lighting permutations between those modules.

By precalculating lighting interactions across modular configurations, the engine aims to significantly reduce real-time lighting costs while preserving visual fidelity.

Performance and optimization are central priorities. The architecture is being designed with careful attention to efficiency, including optimization strategies specifically targeting AMD hardware.

Intended Features
Tsu Game Engine is planned to include:

A modular Entity Component System (ECS)

A custom editor with scene manipulation tools

Clear separation between Editor Mode and runtime Game Mode

Visual transform gizmos (move, rotate, scale)

A modular scripting system

Scene serialization

A material and shader system

A module-based procedural generation pipeline

A lighting system capable of precomputed modular light permutations

A rendering pipeline designed for high performance and hardware-aware optimization

Purpose
Tsu is both a purpose-driven solution for a specific game design challenge and an evolving engine architecture project. What began as a necessity to enable a game concept has grown into a deliberate effort to design and build a fully independent game engine from the ground up.
