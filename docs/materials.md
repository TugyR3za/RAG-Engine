# Per-object base-color materials

Every renderable entity owns a small, editable material: an RGB **base color**
and a **Use Base-Color Texture** toggle. It is deliberately not a PBR system or a
material asset; it tints or replaces the texture the object already resolves
from its mesh (the shared checker for cubes/sphere/ground, the embedded image
for the duck).

| Setting | Texture used | Result (before lighting and shadows) |
|---|---|---|
| texture on (default) | mesh's existing texture | texture × base color |
| texture off | none | base color |
| texture unavailable | 1×1 white fallback | base color |

Defaults are white and texture on, which reproduces the appearance from before
materials existed. Lighting, shadows, skybox, selection outlines, gizmos and
opaque depth behavior are unchanged; there is no transparency.

## Editing

Select a renderable in the Hierarchy (or click it in the viewport). The
Inspector shows a **Material** section below **Renderable** with:

- **Base Color**: picker plus R/G/B fields (double-click a field to type).
- **Use Base-Color Texture**: checkbox; the line below names the texture the
  object resolved and marks it "(not used)" when off.
- **Reset to Defaults**: back to white + texture on (disabled when already default).

Edits take effect on the next frame. Non-renderable entities (camera, light) show
no Material section. Spawned objects start with the same defaults.

## Color convention

The Inspector, the component and the scene file all store the base color as
**sRGB-encoded** values in [0, 1]: the same space as texture pixels and as the
picker's 0-255 / hex display, so `#FF0000` is `[1, 0, 0]` and 0.5 means `0x80`.

Exactly one conversion happens per input, plus one output encode:

1. `Scene::ExtractRenderWorld` converts the base color to linear with
   `renderer::SrgbToLinear` (piecewise IEC 61966-2-1, not a 2.2 power) and stores
   it in `RenderObject::base_color_linear`.
2. The base-color texture is `VK_FORMAT_R8G8B8A8_SRGB`, so the sampler returns
   linear values.
3. `Triangle.frag` multiplies the two linear values and lights the result.
4. The sRGB swapchain encodes the output.

Known limitation: ImGui draws its widgets straight into the sRGB swapchain without
linearizing, so the picker *swatch* of a mid-tone color appears lighter than the
value it represents (this already applies to the Light color). Pure primaries,
white and black are unaffected, and the stored numbers are always the sRGB values
shown in the fields. The Light color, unlike the base color, is treated as linear.

## Data flow and ownership

- `renderer::MaterialSettings` (`src/Rag/Renderer/Material.h`): backend-neutral
  authoring value, stored by value in `scene::RenderableComponent::material_settings`.
  Entities sharing a mesh or texture never share material state; destroying an
  entity destroys its material, and recycled entity slots start from defaults.
- `renderer::RenderObject::base_color_linear` / `use_base_color_texture`: the
  per-draw form produced by scene extraction.
- Vulkan (`VulkanRenderer.cpp`, `VulkanGraphicsPipeline.cpp`): per draw, the object
  binds either its mesh's existing texture descriptor or the shared white one,
  and pushes its tint. No texture is reloaded or duplicated per object, and no
  shared mesh/texture/descriptor is modified.
- Push constants: bytes 0-63 are the vertex-stage model matrix (unchanged), bytes
  64-79 a fragment-stage `vec4 base_color_factor` (rgb tint, a unused). 80 bytes
  total, within the 128-byte minimum every Vulkan device guarantees (static_assert).
- The white texture is texture slot 0, created from memory at renderer start-up
  (no disk I/O) and owned for the renderer's lifetime. If `assets/texture.png`
  fails to load, meshes that use it now draw with the white fallback instead of
  aborting renderer start-up.

## Scene files

Material values are additive version-1 fields inside the existing renderable
`"material"` object, which already carried the (unused) handle passthrough:

```json
"material": {
  "index": 4294967295,
  "generation": 0,
  "baseColor": [1.0, 0.0, 0.0],
  "useBaseColorTexture": false
}
```

`sceneVersion` stays 1. Files without these fields (including the current
`scenes/default.ragscene`) load with the defaults. An invalid `baseColor` (not three
numbers) or `useBaseColorTexture` (not a boolean) logs a warning and falls back to
that field's default without rejecting the scene; colors outside [0, 1] are clamped.
Mesh and texture identifiers are unchanged and no GPU handles are written.

## Tests

`RagSceneTests` covers defaults, the sRGB conversion, extraction of per-object
values, independence of entities sharing one mesh, reset, and recycled entity
slots. `RagSceneSerializationTests` covers the round-trip, legacy files without
the fields, and malformed/out-of-range values. `RagSkyboxTests --gpu-smoke` draws
an untextured red cube and a tinted textured duck under Vulkan validation.
