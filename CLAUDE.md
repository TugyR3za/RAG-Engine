# RAG Engine — project instructions

Vulkan-first C++20 engine on Windows. Read `README.md` for features and controls.

## Build directory (canonical)

- The ONE development build is `build/skybox` (Visual Studio 18 2026, x64,
  multi-config). Debug executable: `build/skybox/Debug/RagSandbox.exe`.
- Do not create feature-specific build folders (build/playmode, build/dev, ...).
  `build/`, `build-vs2026/` and old executables are legacy; cleanup is deferred.
- `build-vs2026/_deps` holds the offline Jolt/ImGui/ImGuizmo sources the
  canonical cache uses (`FETCHCONTENT_FULLY_DISCONNECTED=ON`). Do not move it.

## Commands (run from the repository root)

```powershell
cmake --build .\build\skybox --config Debug --parallel           # everything
ctest --test-dir .\build\skybox -C Debug --output-on-failure     # all tests
ctest --test-dir .\build\skybox -C Debug -LE gpu                 # skip window/GPU tests
.\tools\build-and-run.ps1                                        # build + launch editor
.\tools\build-and-run.ps1 -SmokeTest                             # short lifecycle smoke run
.\build\skybox\Debug\RagSandbox.exe --play-mode-self-test        # scripted Play/Pause/Stop check
```

Launch the sandbox with the repository root as the working directory; scenes
load from and save to the source `scenes/` folder directly.

## Module boundaries

- `RagCore` / `RagPlatform` / `RagRuntime`: base types, Win32 window, input, app loop.
- `RagRenderer`: backend-neutral render data only. `RagRendererVK`: every Vulkan
  type stays inside it. The renderer never owns physics or simulation policy.
- `RagScene`: entities, components, extraction, JSON (de)serialization model.
- `RagPhysics`: Jolt behind a PIMPL; depends on `RagCore` only.
- `sandbox/`: the glue. Owns scene <-> physics mapping, the editor UI
  (`RAG_EDITOR`), and the Edit/Playing/Paused lifecycle
  (`SimulationController.h`). Header-only sandbox helpers stay engine-free so
  tests can include them.

## Play mode (see docs/play-mode.md)

- The editor starts in Edit; physics only advances while Playing (or one tick per
  Paused Step). Keep that gate in `SandboxClient::UpdateSimulation`; never step
  physics from scene creation, load, or restore.
- Lifecycle requests are applied at the safe point at the top of `OnUpdate`,
  never from inside the renderer's UI callback.
- Anything new that is authored per entity must round-trip through
  `CaptureScene` / `ApplyScene`, or Stop will silently drop it.
- Scene document operations (New/Load/Save/Save As) must stay refused outside
  Edit (`RefuseDocumentOperation`).

## First-person player (see docs/player-controller.md)

- Authoring = `PlayerStartComponent` (RagScene, clamped by `SanitizePlayerStart`).
  Collision = `PhysicsWorld` character API (Jolt `CharacterVirtual`; not a body,
  counted by `CharacterCount`). Rules = engine-free `sandbox/PlayerController.h`.
- The player is runtime-only: spawned on Play, destroyed in `ClearSceneState`,
  never captured/saved. Keep the character substeps inside `UpdateSimulation`'s
  advance (no separate scheduler).
- The OS cursor is only captured after a click on the game view; Escape, Pause,
  Stop and focus loss release it. The self-test captures logically only
  (`OsCursorCaptureAllowed`): never hide/confine the real cursor from tests.

## Scene documents (see docs/scene-documents.md)

- Document identity lives in `rag::runtime::SceneDocument` (`document_`), not a
  raw path. New Scene and the built-in startup fallback are Untitled; Save on
  Untitled writes nothing and opens the Save As prompt. Save As / Load adopt a
  path only after the write/read succeeded.
- Never make Save (or an empty Save As name) fall back to the previous file
  or `default.ragscene`.
- Self-test/document tests write only to a fresh temp folder; plain Save is
  only exercised while the document is a temp file or Untitled.

## Conventions

- Match the surrounding style: 4-space indent, Allman braces, `snake_case_`
  members, explanatory comments on non-obvious behavior.
- Scene files in `scenes/` are user data: never overwrite them from tests or
  temporary runtime state. Play mode snapshots live in memory only.
- Keep Vulkan validation clean (Debug enables it; errors log as `[Error] [Vulkan]`).

## Deferred / out of scope (unless explicitly requested)

- Scripting (language undecided), PBR, general undo/redo, network play, old
  build-folder cleanup. Player extras beyond walk/look/jump (stairs, crouch,
  moving platforms, interaction, third-person) are also deferred.
- Unsaved-change (dirty) tracking: New/Load/quit discard edits without a prompt.
