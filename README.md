# RAG Engine

RAG Engine is a Vulkan-first C++ game engine being built in deliberate phases.

## Current Milestone

Phase 1 establishes the foundation:

- CMake project structure
- Core logging and assertions
- Deterministic application loop
- High-resolution frame timing
- API-agnostic events and input state
- Win32 platform window backend
- Sandbox executable

The renderer is intentionally not implemented in Phase 1. The Platform layer exposes native window handles that a future Vulkan backend can consume without leaking Vulkan types into game logic.

Phase 2 adds a Vulkan renderer foundation behind the backend-neutral `rag::renderer::IRenderer` API. Phase 4 adds the first runtime object model with entities, transforms, cameras, renderables, lights, and backend-neutral render extraction.

See `docs/phase-1-foundation.md`, `docs/phase-2-vulkan-renderer.md`, `docs/phase-4-scene-runtime.md`, and `docs/engine-architecture-roadmap.md` for the current architecture notes.

## Build

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\RagSandbox.exe
```

Vulkan is enabled by default in CMake:

```powershell
cmake -S . -B build -DRAG_ENABLE_VULKAN=ON
```

If the Vulkan SDK is not installed yet, configure the non-renderer fallback:

```powershell
cmake -S . -B build -DRAG_ENABLE_VULKAN=OFF
```

This workspace currently targets Windows in Phase 1.
