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
- Scene-level cubemap skybox with a bundled procedural sky, JSON persistence, and an Inspector toggle. See [skybox setup, face conventions, commands and validation](docs/skybox.md).
- Textured materials using Vulkan sampled images and per-texture descriptor sets.
- Per-object base-color materials (sRGB tint + texture toggle) edited in the Inspector and saved with the scene. See [material behavior, color convention and data flow](docs/materials.md).
- Hardcoded cube and ground geometry.
- `.obj` loading for `models/sphere.obj`.
- `.glb` loading for `models/Duck.glb`, including its embedded base-color texture.
- Scene runtime with entities, transforms, cameras, renderables, lights, and render extraction.
- Free-fly camera controls in the sandbox.
- Jolt Physics integration with gravity, static ground collision, dynamic boxes, and a dynamic sphere.
- Editor Play / Pause / Step / Stop scene testing: the editor starts in Edit (nothing simulates), Play snapshots the scene in memory, Stop restores it exactly. See [play mode lifecycle, timing, restore guarantees and limitations](docs/play-mode.md).
- First-person player: a Player Start (Hierarchy > Add Player Start, saved with the scene) spawns a Jolt capsule character on Play with WASD walking, mouse look, grounded jumping, collision with static and dynamic objects, and a fall reset. Scenes without one keep the free-fly camera. See [player start, controls, camera policy and limitations](docs/player-controller.md).

## Modules

- `RagCore`: base types, logging, assertions, clock, input state, events, math, and application loop.
- `RagPlatform`: platform abstraction for windows and native handles.
- `RagPlatform/Win32`: Win32 window, input translation, resize/fullscreen events.
- `RagRenderer`: backend-neutral renderer API, render world structs, mesh/material handles, lights, cameras, and stats.
- `RagRendererVK`: Vulkan backend. Owns Vulkan instance/device/surface/swapchain, pipelines, shaders, buffers, textures, shadow maps, mesh loading, and GPU submission.
- `RagScene`: entity IDs, component storage, transforms, cameras, renderables, lights, scene update, and render-world extraction.
- `RagPhysics`: Jolt-backed physics world and first-person character controller, hidden behind engine types and opaque body/character handles.
- `sandbox`: sample application that wires the scene, renderer, camera controls, assets, and physics together.

## Source Assets And Scenes

The repository folders `assets/`, `models/`, and `scenes/` are the source of
truth and should be committed. In particular, the starter content includes:

- `assets/texture.png`
- `assets/skyboxes/starter/`
- `models/sphere.obj`
- `models/Duck.glb`
- `scenes/default.ragscene`
- `third_party/stb_image.h`
- `third_party/tiny_obj_loader.h`
- `third_party/tiny_gltf.h`
- `third_party/json.hpp`

Each canonical build refreshes generated runtime copies of `assets/`, `models/`,
and compiled shaders beside `RagSandbox.exe`. Do not edit those copies. Scenes
are not copied during the build: the canonical launcher uses the repository root
as its working directory, so scene loading and explicit editor saves use the
source `scenes/` directory directly. Launched directly from `build\skybox\Debug`,
the sandbox finds no `scenes\default.ragscene`. It then starts from the built-in
scene as an Untitled document, and its startup log names every path it checked.
See [scene documents](docs/scene-documents.md).

Generated files such as `build/`, `.spv` shader outputs, `.vs/`, `.vscode/`,
`.exe`, `.pdb`, and other local artifacts are ignored.

## Requirements

- Windows
- Visual Studio with C++ desktop build tools (the current canonical cache uses
  Visual Studio 18 2026)
- CMake 3.22 or newer
- LunarG Vulkan SDK
- Git
- Internet access on first configure when the retained dependency sources are
  unavailable, because CMake fetches Jolt Physics, Dear ImGui, and ImGuizmo

## Canonical Development Build And Run

The one development build directory is `build/skybox`, and its Debug executable
is `build/skybox/Debug/RagSandbox.exe`. From the repository root, use:

```powershell
.\tools\build-and-run.ps1
```

The script resolves the repository from its own location, builds the exact Debug
target, stops on any configure/build failure, then launches that executable with
the repository root as its working directory. Both the script and application
log the executable, working directory, configuration, and build identity.

If `build/skybox/CMakeCache.txt` does not exist, the script configures it. When
all three retained dependency source trees are present under
`build-vs2026/_deps`, it uses them with FetchContent's fully disconnected mode;
otherwise the first configure may need network access. Do not delete or move
`build-vs2026/_deps` while the canonical cache or offline configure depends on it.

Visual Studio users may open `build/skybox/RAGEngine.slnx` after configuration.
CMake marks `RagSandbox` as the startup project and sets its debugger working
directory to the repository root. The PowerShell command above remains the
primary workflow.

## Tests

Build and run the complete Debug test suite:

```powershell
cmake --build .\build\skybox --config Debug --parallel
ctest --test-dir .\build\skybox -C Debug --output-on-failure
```

`SandboxPlayMode` opens the editor window for a few seconds to run the scripted
Play/Pause/Step/Stop and first-person player checks
(`RagSandbox --play-mode-self-test`, in-memory scenes only). It is labelled
`gpu`; skip it on a machine without a GPU/desktop with:

```powershell
ctest --test-dir .\build\skybox -C Debug --output-on-failure -LE gpu
```

Run the sandbox smoke test (one rendered frame each in Edit, Playing, then Edit
again after Stop):

```powershell
.\tools\build-and-run.ps1 -SmokeTest
```

For an intentionally separate, non-canonical experiment without Vulkan or
physics, use a different build directory so the canonical cache is not changed:

```powershell
cmake -S . -B build/no-vulkan -DRAG_ENABLE_VULKAN=OFF -DRAG_ENABLE_PHYSICS=OFF
cmake --build build/no-vulkan --config Debug --parallel
```

## Controls

- `W`, `A`, `S`, `D`: move camera
- `Space` or `E`: move up
- `Left Ctrl` or `Q`: move down
- Hold right mouse button: mouse look
- Hold `Left Shift`: sprint
- `F5`: Play (in Edit) / Stop (restores the pre-play scene)
- `F6`: Pause / Resume
- `F7`: Step one 1/60 s tick (while Paused)
- `Delete`: delete the selected entity (Stop brings back entities deleted during a run)
- `F1`: hide/show the editor UI (game view); does not change Play/Pause/Stop
- `F11`: toggle fullscreen
- `Escape`: release the player's mouse if it is captured; otherwise quit

With a Player Start, Play shows the player's view instead:

- Left-click the game view: capture the mouse and control the player
- `W`, `A`, `S`, `D`: walk; mouse: look; `Space`: jump
- `Escape`: release the mouse (does not quit); Pause, Stop and focus loss also release it
- Paused: the editor camera inspects the scene from the player's eyes; Resume returns to the player

The same Play / Pause / Step / Stop controls are buttons in the editor menu bar
and items in its Simulation menu. File > New/Load/Save are disabled until Stop.

File > New Scene makes an Untitled document, shown as `Scene: Untitled` in the
menu bar. Save on an Untitled scene opens a Save As prompt and never reuses the
previously open file. Save As and Load adopt a file only when they succeed.
See [scene documents](docs/scene-documents.md). There is no unsaved-change
tracking yet.

## Git Milestone

Recommended commit message:

```text
Milestone: renderer + models + textures + shadows + physics
```
