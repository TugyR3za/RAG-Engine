# RAG Engine Phase 4: Runtime Object Model

## Goal

RAG Engine starts with a simple scene plus component registry, not a full ECS framework. The storage is ECS-compatible from the beginning:

```text
Scene
    Entity registry
    Dense transform storage
    Dense camera storage
    Dense renderable storage
    Dense light storage
```

Scene graph behavior is limited to transform parent/child relationships. Gameplay and render data live in components.

## Module Boundary

```text
src/Rag/Scene
    Entity IDs, component storage, transforms, cameras, renderables, lights,
    scene update, render extraction.

src/Rag/Renderer
    Backend-neutral render submission data: RenderWorld, RenderObject,
    RenderCamera, RenderLight, mesh/material handles.

src/Rag/RendererVK
    May consume RenderWorld later, but Scene never depends on RendererVK.
```

## Runtime Flow

```text
1. Application polls platform input.
2. Scene updates transform world matrices.
3. Scene resolves the active camera.
4. Scene extracts a backend-neutral RenderWorld.
5. Renderer receives RenderFrameContext with optional RenderWorld.
```

## Current Rules

- Entity handles are index + generation.
- Destroyed entity handles become stale and fail `Scene::IsAlive`.
- Destroying a transform parent destroys its child transform subtree.
- Components are stored in dense arrays with sparse entity-to-component lookup.
- Scene components contain engine-level handles only, not Vulkan objects.
- Renderer-facing extraction uses `renderer::RenderWorld`.

## Deferred

- Archetype ECS.
- Query views.
- Jobified system scheduling.
- Scripting, physics, animation, prefabs, and scene serialization.
- Renderer-side consumption of render objects for actual mesh drawing.
