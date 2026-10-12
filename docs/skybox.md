# Skybox background

One scene-level cubemap provides the distant background. It is not an entity or
physics body and does not participate in illumination, shadows or reflections.
The shipped `scenes/default.ragscene` enables the starter sky. Saved scenes next to
the executable still take precedence over the repository default; they are never
automatically upgraded or overwritten. Run from the repository root to find the
shipped scene. If no usable default exists, startup builds an in-memory example
with the starter sky; only an explicit editor save writes a scene.

## Authoring and persistence

The Inspector's **Environment** section appears above the selected entity's
properties, even with no selection. It has **Skybox Enabled** and six read-only
asset references. F1 hides/shows the existing editor; the sky works in both modes.
Changes take effect on the next extracted frame. File > Save / Save As persists
the checkbox and references through the existing scene serialization path.

```json
"environment": {
  "skyboxEnabled": true,
  "skyboxFaces": [
    "assets/skyboxes/starter/px.png",
    "assets/skyboxes/starter/nx.png",
    "assets/skyboxes/starter/py.png",
    "assets/skyboxes/starter/ny.png",
    "assets/skyboxes/starter/pz.png",
    "assets/skyboxes/starter/nz.png"
  ]
}
```

This is an optional additive field in **sceneVersion 1**. Supported version bounds
remain 1..1. Missing environment fields, an empty environment object, and new empty
scenes default to disabled with no face paths, preserving the old background.
Older readers ignore the optional field. Disabling a configured sky preserves its
paths so it can be enabled again after save/load. Only authoring values are saved.

Use project-relative paths with forward slashes, no drive/absolute prefix, empty
segments, `.` or `..`. All six paths are required for a configured environment.
Resolution follows existing asset loading: executable directory first, then
working directory. The existing CMake asset-copy step includes the starter files.
There is no file picker; edit references in scene JSON to use another cubemap.

Malformed environment descriptions log an error and reset to disabled while the
rest of the scene loads. Missing/undecodable/non-square/mismatched images keep the
authoring description but render the clear color. Errors identify the path.
Failures are attempted once per unchanged description, not once per frame; toggle
off/on or change the references to retry after fixing files. Loading a scene
without an environment clears previous settings/resources on the next render.

## Image convention and color

The engine is right-handed, world +Y is up, and an unrotated camera looks down -Z.
Each source file is an ordinary image with its first row at the top. Decode it
without any flips or rotations. For a pixel center define
`u = 2*(column+0.5)/width-1`, `v = 2*(row+0.5)/height-1`, then normalize the direction
below. The top/right columns describe directions toward that edge from the face
center (not another camera's screen axes).

| Index | Face / file | Direction before normalization | Image top | Image right |
|---|---|---|---|---|
| 0 | +X / px.png | (1, -v, -u) | +Y | -Z |
| 1 | -X / nx.png | (-1, -v, u) | +Y | +Z |
| 2 | +Y / py.png | (u, 1, v) | -Z | +X |
| 3 | -Y / ny.png | (u, -1, -v) | +Z | +X |
| 4 | +Z / pz.png | (u, -v, 1) | +Y | +X |
| 5 | -Z / nz.png | (-u, -v, -1) | +Y | -X |

This matches [Khronos cube-map face selection and edge handling](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/chapters/textures.adoc#cube-map-face-selection-and-transformations).
The generator uses this same direction mapping on every face. Edge texels are
center samples, not duplicated border texels; seamless cube filtering combines
adjacent faces. See the [starter asset source/license](../assets/skyboxes/starter/README.md).

Color follows existing texture rendering: decoded RGBA8 bytes use
`VK_FORMAT_R8G8B8A8_SRGB`, so texture sampling converts RGB to linear values. The
sky shader outputs those values directly; the preferred sRGB swapchain performs
output encoding. There is no extra gamma operation, exposure or tone mapping. An
unusual non-sRGB fallback swapchain retains the same color limitation as the
existing material renderer. Supply LDR sRGB images, not HDR radiance data.

## Rendering and lifetime

`EnvironmentSettings` is a backend-neutral value owned by `Scene`, copied into
`RenderWorld`, and also used by `SerializedScene`. Vulkan stays in RagRendererVK.
`VulkanCubemapData` validates all six headers before allocating/decoding pixels
using the existing stb_image implementation and asset resolver. Face dimensions
must be square, equal, positive and within the selected device's cube-image limit.

`VulkanTexture` reuses its existing staging, upload barriers, cleanup and sRGB
format for a six-layer cube-compatible image, cube view and linear clamp sampler.
All six layers transition undefined -> transfer destination -> shader read.
The existing texture-descriptor owner manages the cubemap's descriptor set.
Partial creation uses the existing exception cleanup pattern.

The sky pipeline reconstructs view rays from the active camera projection (already
Y-flipped by the existing Vulkan UBO update) and uses only the rigid view rotation.
Translation never enters the sky direction. It draws a full-screen triangle at
depth 1 after scene geometry, with LESS_OR_EQUAL testing and depth writes disabled,
before selection outlines/debug/UI overlays. The existing RH_ZO projection,
depth-clear 1 and geometry LESS comparison are preserved. Orthographic cameras
have parallel rays, so their infinitely distant sky is a single direction/color.

Environment changes wait for the device before retiring pipeline, descriptor pool
and image; shutdown already waits idle. This small synchronous load path can hitch
when toggling/loading, but never destroys resources still in flight. Unchanged
settings (including failed loads) allocate nothing. Resize uses dynamic viewport
and scissor plus the active camera's updated aspect. Attachment-format changes
invalidate the sky pipeline/resources for recreation; normal resizing retains
the cubemap. Shared immutable assets may be retained across scenes with identical
environment settings.

Stricter testing exposed an existing shared-depth-image synchronization hazard,
reproduced before loading any skybox. `VulkanSwapchain` now includes late depth
tests and the previous depth-write access in its external dependency, ordering
each frame's depth transition/clear after the prior frame's stores. No attachment
ownership or render-pass architecture was changed.

## Commands

From `D:\Projects\RAG Engine`, the exact configuration used here reuses existing
dependency checkouts and avoids the stale build cache's old source location:

```powershell
cmake -S . -B build/skybox -G "Visual Studio 18 2026" -A x64 -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_JOLTPHYSICS="D:/Projects/RAG Engine/build-vs2026/_deps/joltphysics-src" -DFETCHCONTENT_SOURCE_DIR_IMGUI="D:/Projects/RAG Engine/build-vs2026/_deps/imgui-src" -DFETCHCONTENT_SOURCE_DIR_IMGUIZMO="D:/Projects/RAG Engine/build-vs2026/_deps/imguizmo-src"
cmake --build build/skybox --config Debug --parallel 8
ctest --test-dir build/skybox -C Debug --output-on-failure
.\build\skybox\Debug\RagSandbox.exe
.\build\skybox\Debug\RagSandbox.exe --smoke-test
.\build\skybox\Debug\RagSkyboxTests.exe --gpu-smoke
python tools/generate_starter_sky.py --check
```

For a fresh checkout with no cached dependencies, configure instead with
`cmake -S . -B build/skybox -G "Visual Studio 18 2026" -A x64`; the existing pinned
FetchContent dependencies require network access. Visual Studio 2022 installations
can use generator `Visual Studio 17 2022` in a separate build directory.

Individual CPU test commands are `RagSceneTests.exe`,
`RagSceneSerializationTests.exe`, `RagPhysicsTests.exe`, and `RagSkyboxTests.exe`
under `build/skybox/Debug/`. CTest runs all four. GPU smoke is deliberately opt-in;
it requires a Windows desktop/Vulkan device and briefly changes fullscreen mode.
Its exit status covers exceptions, not validation callbacks: inspect the log for
`[Error] [Vulkan]`, `VUID-`, and `SYNC-HAZARD`.

To enable synchronization validation in addition to ordinary Debug validation:

```powershell
$env:VK_LAYER_ENABLES = 'VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT'
.\build\skybox\Debug\RagSkyboxTests.exe --gpu-smoke *> build/skybox/skybox-sync-smoke.log
Remove-Item Env:VK_LAYER_ENABLES
```

The bad-asset cases intentionally emit application errors. There should be exactly
12 `Skybox starting at` fallback messages: four invalid descriptions in each of
three cycles, despite rendering four frames per description.

## Review map

| Files | Purpose |
|---|---|
| `src/Rag/Renderer/Environment.h`, `.cpp` (new) | Neutral settings and portable face-path validation. |
| `src/Rag/Renderer/Renderer.h` | Render-world environment snapshot/reset. |
| `src/Rag/Scene/Scene.h`, `.cpp` | Scene ownership/access and render extraction. |
| `src/Rag/Scene/SceneSerialization.h`, `.cpp` | Optional v1 environment persistence and tolerant fallback. |
| `src/Rag/RendererVK/Internal/VulkanCubemapData.h`, `.cpp` (new) | CPU decoding, six-face validation, useful path errors. |
| `src/Rag/RendererVK/Internal/VulkanTexture.h`, `.cpp` | Reuse image ownership/upload for cubemaps. |
| `src/Rag/RendererVK/Internal/VulkanSkyboxPipeline.h`, `.cpp` (new) | Background pipeline and binding. |
| `src/Rag/RendererVK/Shaders/Skybox.vert`, `.frag` (new) | Translation-free camera rays, far depth, cube sampling. |
| `src/Rag/RendererVK/VulkanRenderer.cpp` | Draw order, live changes, failure caching, cleanup and resize handling. |
| `src/Rag/RendererVK/Internal/VulkanSwapchain.cpp` | Narrow fix for the existing shared-depth synchronization hazard. |
| `sandbox/SandboxMain.cpp` | Environment UI, capture/apply/reset, in-memory startup fallback. |
| `scenes/default.ragscene` | Add only the starter environment; preserve existing entities. |
| `assets/skyboxes/starter/{px,nx,py,ny,pz,nz}.png` (new) | Six small, original sRGB sky faces. |
| `assets/skyboxes/starter/README.md` (new) | Asset provenance, CC0 dedication and generation. |
| `tools/generate_starter_sky.py` (new) | Reproducible offline generator/check, standard library only. |
| `tests/SceneSerializationTests.cpp` | Enabled/file round-trip, disabled reference retention, old-scene reset, malformed data. |
| `tests/SceneRuntimeTests.cpp` | Environment extraction and stale-state reset. |
| `tests/SkyboxTests.cpp` (new) | Image decoding/failure tests and optional multi-frame GPU sequence. |
| `CMakeLists.txt` | Sources, shader compilation/copy, test target and CTest registration. |
| `README.md`, `docs/skybox.md` | Feature entry point and this implementation/validation guide. |

## Validation and limits

See the final task report for executed checks. Manual acceptance should include:

1. Fly/rotate around the default scene, inspecting the horizon and all face edges.
   Translate without rotating: the sky should have no parallax.
2. Check textures, ground, lighting, shadows, selected-object outlines and gizmos
   against the sky; spawn objects and switch F1 between editor and game modes.
3. Resize, minimize/restore, and toggle F11 fullscreen while moving the camera.
4. Toggle Inspector > Environment > Skybox Enabled. Save As a separate test scene,
   close/reopen, and load it; verify both disabled and enabled settings restore.
5. Load a v1 scene without environment fields after a sky scene. Check clear color.
   Try missing, corrupted, rectangular and mismatched images in copies of scenes;
   entities must still load and logs must identify the failed path without frame spam.
6. Alternate scenes repeatedly under validation. The GPU smoke automates the
   resource/resize parts but does not assert rendered pixel correctness or editor interaction.

Limitations: one LDR cubemap, one mip level, no picker, synchronous loading, and no
automatic retry/file watching. The sky has no influence on current lighting.
No HDR/tone mapping, IBL, reflections, atmospheric simulation or AA changes.
