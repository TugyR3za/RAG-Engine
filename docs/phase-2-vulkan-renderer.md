# RAG Engine Phase 2: Vulkan Renderer Foundation

## Goal

Phase 2 adds the first real rendering backend while preserving the engine architecture:

```text
Sandbox / Game
    -> rag::renderer::IRenderer
        -> rag::renderer::vk::VulkanRenderer
            -> Vulkan instance/device/swapchain/commands/sync
```

The public renderer interface remains backend-agnostic. Vulkan handles are private to `RendererVK`.

## Module Boundary

```text
src/Rag/Renderer
    Engine-facing renderer contracts.
    No Vulkan/OpenGL/DirectX headers.

src/Rag/RendererVK
    Vulkan backend factory and implementation.
    May include Vulkan headers.

src/Rag/RendererVK/Internal
    RAII wrappers for Vulkan objects.
```

## Class Responsibilities

```text
renderer::IRenderer
    Backend-neutral frame rendering contract.

renderer::RendererDesc
    Backend-neutral construction data: window, validation flag, frame count.

renderer::RenderFrameContext
    Backend-neutral per-frame data. Currently clear color only.

vk::VulkanRenderer
    Public Vulkan backend implementation. Uses pimpl so its public header does
    not expose Vulkan types.

vk::VulkanInstance
    VkInstance creation, required instance extensions, validation layer setup,
    debug messenger lifetime.

vk::VulkanSurface
    Platform-native surface creation. On Windows this creates VkSurfaceKHR from
    HINSTANCE/HWND acquired through platform::NativeWindowHandle.

vk::VulkanDevice
    Physical device scoring/selection, queue family discovery, logical device,
    graphics/present queues.

vk::VulkanSwapchain
    Surface format, present mode, extent, swapchain images, image views,
    render pass, framebuffers, resize recreation.

vk::VulkanFrameResources
    Per-frame command pools, command buffers, semaphores, and fences.
```

## Initialization Order

```text
1. Platform creates the native window.
2. Sandbox constructs RendererDesc using platform::IWindow.
3. VulkanRenderer creates VulkanInstance.
4. VulkanSurface creates VkSurfaceKHR from NativeWindowHandle.
5. VulkanDevice selects physical device and creates logical device/queues.
6. VulkanSwapchain creates swapchain images, image views, render pass, framebuffers.
7. VulkanFrameResources creates command pools, command buffers, semaphores, fences.
8. Application loop calls IRenderer::RenderFrame.
```

## Shutdown Order

```text
1. Renderer waits for device idle.
2. Frame resources are destroyed.
3. Swapchain resources are destroyed.
4. Logical device is destroyed.
5. Surface is destroyed.
6. Debug messenger and instance are destroyed.
7. Platform window is destroyed.
```

## Error Handling

- Initialization and unrecoverable Vulkan failures throw `vk::VulkanError`.
- `RAG_VK_CHECK` converts failed `VkResult` values into readable exceptions.
- Debug builds enable validation when the sandbox is built with `RAG_DEBUG`.
- Resize and `VK_ERROR_OUT_OF_DATE_KHR` are treated as recoverable swapchain events.
- Destructors do not throw; they only destroy owned Vulkan handles.

## Current Rendering Path

The first rendering path clears the swapchain image using a render pass. It intentionally does not add shaders, descriptors, pipelines, or mesh drawing yet.

This proves:

- instance creation
- validation layer integration
- Win32 surface creation
- physical/logical device selection
- swapchain management
- command buffer recording
- GPU/CPU synchronization
- queue submit and present
- resize handling

## Backend-Agnostic Data

These remain outside Vulkan:

- `RendererDesc`
- `RenderFrameContext`
- `RendererStats`
- `IWindow`
- `NativeWindowHandle`
- future mesh, material, shader, texture, camera, and render graph descriptions

## Backend-Specific Data

These stay inside `RendererVK`:

- `VkInstance`
- `VkSurfaceKHR`
- `VkPhysicalDevice`
- `VkDevice`
- `VkQueue`
- `VkSwapchainKHR`
- `VkImageView`
- `VkRenderPass`
- `VkFramebuffer`
- `VkCommandPool`
- `VkCommandBuffer`
- `VkSemaphore`
- `VkFence`

## Future Extension Points

```text
Descriptor System
    Add descriptor set layouts, descriptor pools, bindless-style allocation,
    and per-frame descriptor recycling inside RendererVK.

Pipeline Cache
    Add VkPipelineCache ownership and serialized cache blobs. Keep pipeline
    descriptions backend-neutral in Renderer.

Shader System
    Assets should load shader bytecode and reflection metadata. RendererVK
    should create VkShaderModule internally.

Material System
    Engine-facing material parameters stay backend-neutral. RendererVK maps
    them to descriptor sets, push constants, and pipelines.

Render Graph
    Renderer module should define passes/resources at engine level. RendererVK
    compiles them into Vulkan render passes, barriers, transient images, and
    command submissions.

GPU Profiling
    Add timestamp query pools and debug labels inside RendererVK. Expose only
    renderer-neutral timing stats through Diagnostics.
```

## Common Phase 2 Mistakes

- Exposing `Vk*` handles through `rag::renderer`.
- Creating Vulkan objects directly in `Application`.
- Making swapchain recreation destroy the whole renderer.
- Recording commands in platform code.
- Adding shader/material abstractions before a clear-screen frame loop works.
- Treating OpenGL limitations as the renderer abstraction baseline.
- Ignoring validation layers until later.
- Forgetting that minimized windows may report zero swapchain extent.
