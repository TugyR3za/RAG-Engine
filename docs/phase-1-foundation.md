# RAG Engine Phase 1: Foundation

## Goal

Phase 1 creates the minimum engine runtime that later rendering, assets, scenes, tools, and diagnostics can stand on:

- Build system
- Logging
- Assertions
- Window creation
- Input collection
- Frame timing
- Application loop

This stage belongs first because every later system needs a reliable process lifetime, a platform window, a place to receive events, and a predictable frame boundary.

## Architecture

```text
Sandbox/Game Code
    depends on
Rag::Core Application, Input, Time, Events
    depends on
Rag::Platform Window Interface
    implemented by
Rag::Platform::Win32 Window Backend
```

Renderer modules are deliberately absent from this phase. In Phase 2, `RendererVK` can consume the platform native window handle to create a Vulkan surface while game code continues to talk only to engine-level interfaces.

## Module Boundaries

```text
src/Rag/Core
    Application loop, logging, assertions, timing, input, event definitions.

src/Rag/Platform
    API-agnostic platform interfaces such as IWindow and NativeWindowHandle.

src/Rag/Platform/Win32
    Windows-specific window creation and message translation.

sandbox
    A small executable proving the engine foundation works.
```

## Phase 1 Milestone

The sandbox opens a native Win32 window, logs startup and shutdown, tracks frame delta time, updates keyboard and mouse state, and exits cleanly when the window is closed or Escape is pressed.

## Future Extension Points

- `RendererVK` will use `NativeWindowHandle` to create a Vulkan surface.
- `InputState` can later route through an action mapping layer.
- `Application` can gain a module stack for renderer, audio, physics, editor, and tools.
- `Event` can become a lock-free or ring-buffered queue if profiling shows dispatch overhead.
- Platform backends can add Linux and macOS implementations without changing game code.

## Risks And Tradeoffs

- The first platform backend is Win32 only. This keeps the milestone dependency-free and compileable without vendoring GLFW or SDL.
- Events use `std::variant`, which is type-safe and allocation-free for these payloads, but a packed custom event buffer may be preferable later for very high event throughput.
- The application client uses a narrow virtual interface. This is a practical boundary for user code now; runtime objects should still move toward component data and composition in later phases.
