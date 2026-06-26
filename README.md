# RAG Engine

RAG Engine is a Vulkan-first C++ game engine built in deliberate milestones. The current sandbox is a working 3D scene with renderer, asset loading, scene extraction, free-fly camera controls, and Jolt-backed physics.

## Current State

The engine currently includes:

- Win32 window creation, input, fullscreen toggle, frame timing, logging, and application loop.
- Backend-neutral renderer interfaces in `RagRenderer`.
- Vulkan renderer backend with validation-layer support in Debug builds.
- Swapchain resize handling, depth buffering, command buffers, sync objects, and per-frame diagnostics.
- Lit 3D rendering with directional diffuse/specular lighting.
- Directional shadow mapping with PCF filtering.
- Textured materials using Vulkan sampled images and per-texture descriptor sets.
- Hardcoded cube and ground geometry.
- `.obj` loading for `models/sphere.obj`.
- `.glb` loading for `models/Duck.glb`, including its embedded base-color texture.
- Scene runtime with entities, transforms, cameras, renderables, lights, and render extraction.
- Free-fly camera controls in the sandbox.
- Jolt Physics integration with gravity, static ground collision, dynamic boxes, and a dynamic sphere.

## Modules

- `RagCore`: base types, logging, assertions, clock, input state, events, math, and application loop.
- `RagPlatform`: platform abstraction for windows and native handles.
- `RagPlatform/Win32`: Win32 window, input translation, resize/fullscreen events.
- `RagRenderer`: backend-neutral renderer API, render world structs, mesh/material handles, lights, cameras, and stats.
- `RagRendererVK`: Vulkan backend. Owns Vulkan instance/device/surface/swapchain, pipelines, shaders, buffers, textures, shadow maps, mesh loading, and GPU submission.
- `RagScene`: entity IDs, component storage, transforms, cameras, renderables, lights, scene update, and render-world extraction.
- `RagPhysics`: Jolt-backed physics world hidden behind engine types and opaque body handles.
- `sandbox`: sample application that wires the scene, renderer, camera controls, assets, and physics together.

## Assets

These folders are part of the project and should be committed:

- `assets/texture.png`
- `models/sphere.obj`
- `models/Duck.glb`
- `third_party/stb_image.h`
- `third_party/tiny_obj_loader.h`
- `third_party/tiny_gltf.h`
- `third_party/json.hpp`

Generated files such as `build/`, `.spv` shader outputs, `.vs/`, `.vscode/`, `.exe`, `.pdb`, and other local artifacts are ignored.

## Requirements

- Windows
- Visual Studio 2022 with C++ desktop build tools
- CMake 3.22 or newer
- LunarG Vulkan SDK
- Git
- Internet access on first configure if `RAG_ENABLE_PHYSICS=ON`, because CMake fetches Jolt Physics `v5.5.0`

## Build And Run

From the repository root:

```powershell
cmake -S . -B build
cmake --build build --config Debug --parallel
.\build\Debug\RagSandbox.exe
```

Run the scene/runtime tests:

```powershell
.\build\Debug\RagSceneTests.exe
```

Run a one-frame sandbox smoke test:

```powershell
.\build\Debug\RagSandbox.exe --smoke-test
```

Build without Vulkan if the Vulkan SDK is unavailable:

```powershell
cmake -S . -B build -DRAG_ENABLE_VULKAN=OFF
cmake --build build --config Debug --parallel
```

Build without physics if you do not want CMake to fetch Jolt:

```powershell
cmake -S . -B build -DRAG_ENABLE_PHYSICS=OFF
cmake --build build --config Debug --parallel
```

## Controls

- `W`, `A`, `S`, `D`: move camera
- `Space` or `E`: move up
- `Left Ctrl` or `Q`: move down
- Hold right mouse button: mouse look
- Hold `Left Shift`: sprint
- `F11`: toggle fullscreen
- `Escape`: quit

## Git Milestone

Recommended commit message:

```text
Milestone: renderer + models + textures + shadows + physics
```
