# Contributing to TSU Engine

Thank you for your interest in contributing! TSU Engine is in active **Alpha** development. All contributions — code, bug reports, documentation, and feedback — are appreciated.

---

## Reporting Bugs

Open an issue on [GitHub Issues](https://github.com/tsuyu122/-tsu-engine/issues) with the following:

- A clear, descriptive title
- Steps to reproduce the bug
- Expected vs. actual behavior
- Your OS and GPU (e.g., Windows 11, NVIDIA RTX 3060)
- Any relevant console output or error messages

If you prefer not to use GitHub, you can also send an email to [vhmarchiore@gmail.com](mailto:vhmarchiore@gmail.com) with the subject **"github tsu engine sugestion"** and describe the bug there.

---

## Suggesting Features

Open an issue with the label `enhancement`. Describe:

- What problem the feature solves
- How you envision it working
- Any alternatives you have considered

Alternatively, send an email to [vhmarchiore@gmail.com](mailto:vhmarchiore@gmail.com) with the subject **"github tsu engine sugestion"**.

---

## Code Contributions

### Build Requirements

- CMake 3.16+
- C++17 compiler (MSVC recommended on Windows; GCC/Clang on Linux)
- OpenGL 4.6 capable GPU

### Building

```bash
git clone https://github.com/tsuyu122/-tsu-engine
cd tsuEngine
cmake -S . -B build
cmake --build build --config Release
```

### Pull Request Guidelines

- Keep PRs focused — one logical change per PR
- Target the `master` branch
- Describe **what** the PR changes and **why**
- If fixing a bug, reference the related issue (`Fixes #123`)
- If adding a new system, include a brief note in the PR body about how it integrates

---

## Code Style

| Rule | Convention |
|---|---|
| Language standard | C++17 |
| Indentation | 4 spaces (no tabs) |
| Naming: types/classes | `PascalCase` |
| Naming: functions/methods | `camelCase` |
| Naming: member variables | `m_PascalCase` |
| Naming: local variables | `camelCase` or `lowerCase` |
| Header guards | `#pragma once` |
| Comments | Prefer descriptive names over comments; add comments for non-obvious logic |

Keep code modular and consistent with the existing architecture (parallel-array ECS, separation of Editor and GameRuntime via `TSU_EDITOR` macro).

---

## Editor vs. GameRuntime

TSU Engine has two build targets:

- **`TsuEngine`** — Full editor (ImGui, gizmos, UI panels). Compiled with `TSU_EDITOR=1`.
- **`GameRuntime`** — Lean player (no ImGui, no editor code).

If your contribution touches shared engine code (e.g., `inputManager.cpp`, `editorCamera.cpp`), wrap any ImGui calls in `#ifdef TSU_EDITOR` so the `GameRuntime` target keeps building cleanly.

---

## Documentation

- Lua API additions should be documented in [`docs/lua/api.md`](docs/lua/api.md)
- New engine systems should have a brief entry in [`docs/index.md`](docs/index.md)
- Update [`README.md`](README.md) if the change affects installation, usage, or the feature list

---

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE).
