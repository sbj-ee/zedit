# zedit

A modal, vim-like text editor written in C++20, built on Dear ImGui + GLFW + OpenGL.
Targets Linux and macOS.

## Status

Early development. See `docs/roadmap.md` for the milestone plan.

## Building

Requires CMake 3.21+, a C++20 compiler, and Ninja (recommended).

```sh
cmake -B build -G Ninja
cmake --build build
./build/frontend_imgui/zedit [file]
```

## Testing

```sh
cmake --build build --target zedit_tests
ctest --test-dir build --output-on-failure
```

## Project layout

- `core/` — the editing engine (buffer, cursor, modal state machine, motions,
  operators, undo, file I/O). No GUI dependencies; independently testable.
- `frontend_imgui/` — the Dear ImGui/GLFW/OpenGL desktop frontend.
- `tests/` — Catch2 unit tests for `core/`.
