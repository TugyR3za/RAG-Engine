# RAG Engine Architecture Roadmap

RAG Engine is Vulkan-first, but engine-facing systems must remain API-agnostic. Backend-specific types stay inside backend modules.

## Long-Term Module Direction

```text
Game / Sandbox / Tests
    |
    v
Editor / Tools Layer
    |
    v
Scene, ECS, Scripting, Serialization
    |
    v
Renderer API Abstraction, Resources, Audio, Physics, Jobs
    |
    v
RendererVK | RendererGL | RendererDX | Platform Backends
    |
    v
Operating System / Graphics API / Hardware
```

The dependency direction is downward only. Core systems do not include Vulkan, OpenGL, or DirectX headers.

## Renderer Boundary

The public renderer layer should expose engine concepts:

- `RenderDevice`
- `Swapchain`
- `Buffer`
- `Texture`
- `Shader`
- `Material`
- `CommandList`
- `RenderPass` or frame graph nodes
- `Fence`, `Semaphore`, or engine-level synchronization handles

`RendererVK` may map these concepts to Vulkan objects, but Vulkan handles should remain private to the backend unless a narrow escape hatch is explicitly required for an advanced feature.

## Phase Roadmap

```text
Phase 1: Foundation
    Build, logging, assertions, platform window, input, timing, application loop.

Phase 2: Vulkan Bring-Up
    Instance, validation layers, surface, device selection, swapchain, commands,
    synchronization, clear screen, triangle.

Phase 3: Resources
    Shader loading, texture loading, mesh loading, materials, asset handles,
    async loading path, file watching.

Phase 4: Scene Runtime
    Transforms, cameras, basic entities/components, renderable objects.

Phase 5: Renderer Scale-Up
    Descriptor management, pipeline cache, material binding model, batching,
    instancing, culling, debug draw, render passes or frame graph.

Phase 6: Tools
    Debug UI, inspector, console, profiling HUD, asset browser.

Phase 7: Runtime Integrations
    Audio, physics integration, animation, scripting hooks, serialization,
    prefabs, scene saving.

Phase 8: Secondary Backends
    OpenGL first, then DirectX, using the renderer abstraction already proven
    by the Vulkan backend.
```

## Performance Rules

- No routine heap allocation in per-frame gameplay paths.
- Prefer handle-based ownership for assets and renderer resources.
- Keep hot runtime data compact and cache-friendly.
- Use RAII for native resources and deterministic teardown.
- Add instrumentation early: logging, assertions, frame timers, GPU markers, validation layers, and profiling hooks.
- Prefer simple data structures first, but choose names and ownership that allow migration to ECS, async loading, and job scheduling.

## Current Status

Phase 1 is implemented as a Win32-first foundation with no external dependencies. Phase 2 has a Vulkan renderer foundation behind backend-neutral renderer contracts. Phase 4 now has a simple scene plus component registry that extracts backend-neutral render data.
