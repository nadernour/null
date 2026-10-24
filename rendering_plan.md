# Rendering Plan

Physically based rendering for Null, split into six phases. Each phase leaves the engine working and can be checked on its own. This file merges the stage 1 plan from the "PBR rendering next steps" session (2026-10-15) with the plan review of 2026-10-18. The README's Rendering section lists the same phases.

## How to Work Through It

- One phase per session, in order. Commit each phase on its own.
- Before starting, read `README.md`, `CLAUDE.md`, **Decisions** below, and the phase. The decisions are settled: change one only for a concrete reason found in the code, and update this file when you do.
- Follow the README's conventions (naming, asserts vs. errors) and the surrounding code's style:
  - designated-initializer `Config` structs for settings with defaults (sizes, formats, flags, names); the objects a class uses, such as `Device`, are constructor parameters, as in `Buffer(Device*, const Config&)`, and a class with no such settings takes plain parameters;
  - `NE_VERBOSE` for allocations;
  - every struct shared with Slang kept in `src/renderer/gpu_types.h`, with its size `static_assert`ed.
- The hardest parts are Phase 1's frame restructuring (synchronization), Phase 3's tangent conventions, and Phase 5's cascade math. If you can spend more effort anywhere, spend it there.
- Every phase ends with this check, on top of its own **Done when** list:
  1. `cmake --build --preset debug && ./build/debug/bin/null_engine --run-tests` passes. Debug builds abort at exit if the validation layer reported any error, sync hazards included.
  2. The `development` and `shipping` presets build.
  3. An interactive run (`./build/debug/bin/null_engine`) survives a few window resizes and exits cleanly.
  4. Before / after screenshots of BasicApp match the phase's expected look.
  5. The phase's README item is ticked and its **Status** below says done.

## Decisions

### Frame and render targets
- **Ownership:** `Application` holds the `Window`, the `Renderer`, and the `Registry`, and runs the loop and the UI hooks. `Renderer` owns everything on the GPU side: the instance, the device, `FrameManager`, `ResourceManager`, `ImGuiManager`, the classes that draw, and the render targets. *Why:* the app holds rendering API objects, not their implementation, so it never has to keep two of them in agreement (such as the formats `ResourceManager`'s pipelines and the targets share), and apps built on the core library don't repeat the setup.
- **No render graph.** `Renderer` is the only place that orders passes; a pass is one `vkCmdBeginRendering` scope, as in Vulkan. It records every barrier, opens every pass (attachments, load ops, clear values, viewport), and hands the classes that draw into a pass their per-frame resources (the command buffer, the frame's upload allocator); those classes only draw. `Image` builds its own barriers from the layout, access, and stage it already tracks, and back-to-back barriers go out together in one `vk_utils::pipelineBarrier`. *Why:* the frame is a fixed sequence, and later a loop over views. One pass can hold several draws (the tonemap and the UI; later the shadow cascades in one atlas), so a pass can't belong to what draws into it. Revisit only if the frame becomes data-driven (user-defined sensor pipelines), many short-lived post passes pile up, or profiling calls for async compute.
- **Per-frame uploads:** each frame in flight has an `UploadAllocator` in `FrameManager`: 16 MB host-visible blocks filled in order, the next block taking over when one is full (an upload larger than a block gets a block of its own size), all reused once that frame's fence has signaled. *Why:* nothing predicts a frame's upload size, so adding a per-frame upload is one call. A prediction that came out short would overflow the buffer in Shipping, where asserts are compiled out.
- **Scene color format:** `VK_FORMAT_R16G16B16A16_SFLOAT`. *Why:* color attachment, blending, and sampling are mandatory for it.
- **Depth format:** `VK_FORMAT_D32_SFLOAT` from `Device::findDepthFormat()`, which falls back only to other depth-only formats; looked up once in `Renderer`. *Why:* nothing uses stencil, and a depth-only image can be sampled through its one view (the Hi-Z pyramid later).
- **Swapchain format:** prefer `VK_FORMAT_B8G8R8A8_UNORM`, and let the tonemap shader apply the sRGB encode. A push-constant flag skips the encode if the surface only offers an `_SRGB` format. *Why:* ImGui's colors are sRGB values meant to be written as-is. On an `_SRGB` swapchain they get encoded twice and look washed out.
- **Sampled render targets:** one bindless slot each, kept across resizes by `BindlessManager::updateSampledImage`. Rewrite a slot only while the device is idle. *Why:* fits the all-bindless design, with no new descriptor mechanism and no extension. Push descriptors stay on the roadmap for compute passes that write storage images.
- **Pipeline layout:** one layout for every graphics pipeline. Set 0 is the bindless set, plus one 128-byte push-constant range for `VERTEX | FRAGMENT`. *Why:* phases 2–4 need fragment-stage push constants anyway, and with one layout set 0 stays bound across passes. This changes the Oct 15 plan, which gave the tonemapper its own layout.
- **Names:** `Renderer` for the frame orchestrator. Classes that draw into a pass are named for what they draw (`SceneRenderer`, `Tonemapper`). `FrameManager` owns the swapchain and the frames in flight.

### Shading and units
- **Tonemapper:** Khronos PBR Neutral, with no exposure inside it. *Why:* it keeps base colors faithful, which camera sensors want.
- **Exposure:** computed from the primary camera's aperture, shutter speed, and ISO (EV100). It's applied as pre-exposure in the scene shader and lands together with physical light units (Phase 4). *Why:* real cameras expose this way, and pre-exposure keeps fp16 from overflowing.
- **Units:**
  - directional lights: lux;
  - point and spot lights: candela;
  - emissive and sky: nits (cd/m²);
  - glTF emissive: multiplied by `kGltfEmissiveNits` (1000).

  *Why:* these are KHR_lights_punctual's units. glTF emissive has no unit, so the constant is the one place to tune it.
- **Light direction:** a light shines along its entity's +X (forward), like cameras.
- **Ambient until IBL:** a sky / ground hemisphere with both a diffuse and a specular term (`envBrdfApprox`). *Why:* without a specular ambient term, metals render black when only punctual lights hit them.
- **Debug views:** a channel view shows what the Khronos Sample Viewer's debug channel shows: data (normals, metallic, roughness, occlusion, UVs) as stored, base color and emissive as colors, and no PBR Neutral. *Why:* the Sample Viewer is the reference renderer, so each view can be checked against it.

### Materials and meshes
- **Material storage:** a device-local buffer of `gpu::Material`, append-only, written at load. The per-instance `colorTint` stays for runtime variation. *Why:* overwriting a slot that an in-flight frame reads needs a barrier, and nothing needs runtime edits before Episodes & Resets.
- **Missing textures:** use the white fallback (index 0). A material flag switches normal mapping on. *Why:* white is neutral for every slot except normals, and it avoids a branch per texture.
- **Samplers:** one immutable sampler per texture filter (linear, nearest) × U wrap × V wrap (repeat, clamp, mirror), 18 in all, then the shadow sampler. A texture's settings index them directly. *Why:* glTF wraps each axis separately, and TextureSettingsTest mixes wrap modes on every test tile. The bindless set layout is update-after-bind, so its samplers count only against the update-after-bind limits, not `maxPerStageDescriptorSamplers` (whose required minimum is 16).
- **Tangents:**
  - use glTF `TANGENT` when present;
  - otherwise generate them with MikkTSpace (vendored `mikktspace.c/.h`), negate `w`, and weld the vertices again.

  *Why:* glTF requires MikkTSpace tangents. glTF normal maps put +Y toward the top of the image, while UV v points down.
- **Missing normals:** the glTF importer generates flat normals (one vertex per index) and drops the file's tangents. *Why:* glTF requires both.
- **Alpha and double-sided:** pipeline variants {opaque, mask, blend} × {cull back, cull none}. Blend draws come after opaque ones, back to front, one draw each. *Why:* opaque draws keep early depth testing, and blending happens in the right order.

### Later phases
- **Shadows:** directional cascaded shadow maps (CSM), 4 cascades in one 4096² D32 atlas, stabilized. They come after the per-view refactor. *Why:* an atlas needs no array images, and per-view drawing is shared with Render Views.
- **IBL:**
  - `.hdr` loaded with stb_image into an RGBA16F cube with prefiltered mips, built with graphics passes;
  - SH9 diffuse computed on the CPU;
  - `envBrdfApprox` instead of a lookup texture.

  *Why:* it reuses color-attachment rendering, so no storage images or compute pipelines are needed yet.

---

## Phase 1: HDR Scene Target & Frame Orchestration
**Status:** done (2026-10-19)

**Goal:** The scene renders into its own HDR color and depth images, a tonemapper writes the result to the swapchain, and ImGui draws on top in the same swapchain pass. `Renderer` runs the whole frame. Nothing changes on screen except the tonemap curve and the UI colors.

This is the Oct 15 stage 1 plan, plus the `Image` barrier helpers, the shared pipeline layout, and a resize test. The review of Oct 21 moved the scene's barriers and rendering scope into `Renderer` and the upload buffer out of `SceneRenderer`. A second review that day moved the GPU setup from `Application` into `Renderer`, renamed `FrameRenderer` to `FrameManager` and `TonemapPass` to `Tonemapper`, replaced the `Config` structs that only held dependencies with constructor parameters, and made depth `D32_SFLOAT`. It also replaced the upload buffer, which `SceneRenderer::prepare` had to size before the frame, with a per-frame block allocator. The frame and tasks below describe the result.

**The frame:**
```
Renderer::render
├─ ResourceManager::flushUploads       everything created since the last frame reaches the GPU
├─ swapchain extent changed? → Device::waitIdle, recreate HDR + depth, updateSampledImage
├─ SceneRenderer::prepare              find the camera, gather + sort draws
├─ FrameManager::beginFrame            wait fence, acquire, reset the frame's upload allocator, begin command buffer
├─ bind bindless set 0 (shared layout)
├─ HDR color, depth → attachments      one barrier batch
├─ scene pass                          clear HDR color + depth
│   └─ SceneRenderer::record           draw
├─ HDR color → SHADER_READ_ONLY,       one barrier batch (swapchain images aren't Image objects)
│  swapchain → COLOR_ATTACHMENT
├─ swapchain pass                      one vkCmdBeginRendering, loadOp DONT_CARE
│   ├─ Tonemapper::record              fullscreen triangle, PBR Neutral, sRGB encode
│   └─ ImGuiManager::draw
├─ swapchain → PRESENT_SRC             manual barrier
└─ FrameManager::endFrame              submit, present
```

**Tasks:**
1. **`Image` barriers** in `src/renderer/image.{h,cpp}`:
   - `transitionBarrier(iNewLayout, iDstAccessMask, iDstStageMask)` builds the barrier from the tracked state (`mCurrentLayout`, `mCurrentAccessMask`, `mCurrentStageMask`) and tracks the new state. The caller records it, batched with the barriers next to it through `vk_utils::pipelineBarrier`.
   - `transition(iCommandBuffer, ...)` records one barrier on its own.
   - The tracked state is private; `StagingManager` builds its upload barriers through these too.

   Every barrier on the HDR color and depth images goes through them:
   - HDR color for the scene pass: `COLOR_ATTACHMENT_OPTIMAL`, `COLOR_ATTACHMENT_OUTPUT`, `COLOR_ATTACHMENT_READ | WRITE`.
   - Depth for the scene pass: `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`, `EARLY | LATE_FRAGMENT_TESTS`, `DEPTH_STENCIL_ATTACHMENT_READ | WRITE`.
   - HDR color for the tonemapper: `SHADER_READ_ONLY_OPTIMAL`, `FRAGMENT_SHADER`, `SHADER_SAMPLED_READ`.
2. **`BindlessManager::updateSampledImage(iIndex, iImageView)`** rewrites a slot that is already registered (assert that it is). Comment that callers must make sure no submitted frame can still read the slot.
3. **Shared pipeline layout** in `ResourceManager`: one push-constant range `{VERTEX | FRAGMENT, 0, 128}`.
   - Rename `getScenePipelineLayout` to `getPipelineLayout` (debug name `Bindless_PipelineLayout`).
   - Every `vkCmdPushConstants` passes `VERTEX | FRAGMENT`; Vulkan requires the flags to match the range.
4. **`Pipeline::Config`** gains `VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT`.
5. **`FrameManager`** keeps only the swapchain, frame sync, and each frame's upload allocator. Remove the depth image, `getDepthImage`, `getDepthFormat`, and `createDepthImage`. **`UploadAllocator`** (`src/renderer/upload_allocator.{h,cpp}`) replaces the upload buffer and `recreateUploadBuffer`:
   - `upload(iData, iSize)` returns the buffer, offset, and device address of the copy;
   - when the current block is full it moves on to the next, creating it the first time (`kUploadBlockSize`, or the upload's size if larger);
   - `beginFrame` calls `reset()` after the frame's fence wait, which reuses every block.
6. **Swapchain:** `Swapchain::Config::preferredFormat` becomes `VK_FORMAT_B8G8R8A8_UNORM`.
7. **`SceneRenderer`** only draws, into the pass `Renderer` opens:
   - `SceneRenderer(iGeometryAllocator)`, the one object it uses.
   - `prepare(ioRegistry, iAspectRatio)` finds the primary camera and gathers and sorts the draws. It runs before `beginFrame`.
   - `record(iCommandBuffer, ioUploadAllocator)` binds the index buffer, uploads the per-draw data, and draws.
   - It no longer knows `FrameManager`, the targets, their barriers, or the load ops.
   - Remove `draw()`, `waitIdle()`, the swapchain code, the ImGui call, and the bindless bind (`Renderer` binds once per frame).
   - The draw-call and instancing code stays as it is.
8. **`Tonemapper`** (`src/renderer/tonemapper.{h,cpp}`) and **`shaders/tonemap.slang`**:
   - `Tonemapper(iDevice, iPipelineLayout, iColorFormat)` builds its `Pipeline`: `shaderName "tonemap"`, the shared layout, the swapchain color format, no depth, `DepthMode::Disabled`, `cullMode VK_CULL_MODE_NONE`.
   - `record(iCommandBuffer, iHdrTextureIndex, iEncodeSrgb)` pushes `gpu::TonemapPushConstants { uint32_t hdrTexture; uint32_t encodeSrgb; }` and draws 3 vertices. The scene shader's push constants are `gpu::ScenePushConstants`.
   - The shader:
     - builds a fullscreen triangle from `SV_VertexID`;
     - reads `gTextures[pc.hdrTexture].Load(int3(int2(position.xy), 0))` (the HDR image and the swapchain are the same size, so it needs no sampler and no UVs);
     - applies PBR Neutral;
     - applies the sRGB encode when `encodeSrgb != 0`.
   - Declare the two bindless arrays in this file too (see the pitfalls).
9. **`Renderer`** (`src/renderer/renderer.{h,cpp}`):
   - `Renderer(iWindow, iAppName)` creates the instance, the surface, the device, `FrameManager`, `ResourceManager`, and `ImGuiManager`, and looks up the depth format. `ResourceManager`'s pipelines get the scene formats: the private `kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT` and the depth format.
   - It owns `SceneRenderer`, `Tonemapper`, the HDR color image (`COLOR_ATTACHMENT | SAMPLED`), the depth image, and the HDR image's bindless slot, registered once.
   - `render(ioRegistry)` runs the frame above: it flushes the uploads, records every barrier, and opens both passes. It compares `FrameManager::getExtent()` with the targets *before* `beginFrame`, so it never waits for idle while recording.
   - `encodeSrgb = !vkuFormatIsSRGB(frameManager->getColorFormat())`.
   - `waitIdle()`, `getResourceManager()`, and `getImGuiManager()` are what `Application` uses.
10. **`ImGuiManager`:** the depth and stencil attachment formats become `VK_FORMAT_UNDEFINED`, because the swapchain pass has no depth image.
11. **`Application`:**
    - holds only the `Window`, the `Renderer`, and the `Registry`; `getResourceManager()` forwards to the renderer;
    - calls `Input::init` before creating the renderer, because ImGui's GLFW backend chains to the input callbacks;
    - `stepFrame()` builds the UI frame through `Renderer::getImGuiManager()`, and `render()` calls `mRenderer->render(*mRegistry)`, which flushes the uploads itself;
    - `waitIdle` calls go to `mRenderer->waitIdle()`;
    - destruction stays in reverse creation order.
12. **Tests** (`src/tests/unit/renderer_test.cpp`, suite `renderer`):
    - **"Frames Render Through Scene, Tonemap, And UI":** `ioContext.app->runForFrames(3)`. In CI this runs the whole path on lavapipe with sync validation.
    - **"A Resize Keeps The HDR Target's Bindless Slot":**
      1. Note `getBindlessManager()->getRegisteredSampledImageCount()`.
      2. Call `glfwSetWindowSize(getWindow()->getGLFWwindow(), 640, 480)` and run a few frames.
      3. Restore the size and run a few frames.
      4. Assert the count is unchanged.

      A tiling window manager may ignore the resize locally; under CI's Xvfb it happens.
    - **"A Frame's Uploads Can Outgrow One Block":** one more cube than a block's worth of `gpu::InstanceData`, behind the camera so they're clipped, then `runForFrames(3)` and remove them. The instance data takes a block of its own and the indirect commands after it start another, so the run checks both ways of adding a block, and the third frame reuses the first frame's blocks.

**PBR Neutral** (Khronos reference; linear in, linear out) **and the sRGB encode:**
```slang
float3 pbrNeutral(float3 color) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;
  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;
  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) {
    return color;
  }
  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  color *= newPeak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

float linearToSrgb(float value) {
  return value <= 0.0031308 ? 12.92 * value : 1.055 * pow(value, 1.0 / 2.4) - 0.055;
}
```

**Done when:**
- The scene looks nearly the same as before.
- The background goes from 0.10 to 0.06 linear (PBR Neutral's toe).
- Bright areas roll off instead of clipping.
- The UI gets darker, as ImGui intends.
- Resizing doesn't change the registered bindless count.

**Pitfalls:**
- `Pipeline` culls back faces with counter-clockwise front faces. The usual fullscreen-triangle formula `uv = float2((id << 1) & 2, id & 2)` gives a clockwise triangle in Vulkan, which gets culled. That's why the tonemapper uses `VK_CULL_MODE_NONE`.
- Bindless descriptors are written with layout `SHADER_READ_ONLY_OPTIMAL`. Transition the HDR image before the swapchain pass.
- Slang reads buffer pointers with natural (scalar) layout, so C++ structs match as-is. Push constants use std430: keep push-constant structs to 64-bit addresses and 32-bit scalars, never `float3`. To check a layout, run `build/debug/_deps/slang-src/bin/slangc <file> -target spirv-asm ...` and read the `OpMemberDecorate ... Offset` lines.
- `CMakeLists.txt` compiles every `shaders/*.slang` file, and each command depends only on its own file. Until a shared module is really needed, duplicate the two bindless declarations. If you add one, exclude it from the compile loop and add it to every shader's `DEPENDS`.
- Both frames in flight share the HDR color and depth images. That's fine: the tracked state makes each frame's first barrier wait for the previous frame's last use.

---

## Phase 2: Materials & glTF Textures
**Status:** done (2026-10-24)

**Goal:** glTF materials and textures reach the GPU.
- A material buffer is indexed per instance.
- The base color, metal-roughness, normal, occlusion, and emissive textures are imported with the right color space and their own sampler settings.
- Shading stays the current fixed light, so every channel except metallic and roughness shows on screen. Debug views show each channel as the Khronos Sample Viewer does.

This is the plan of Oct 15 and 18, checked against the code on Oct 24, after Phase 1. The check made texture wrapping per axis, because TextureSettingsTest, this phase's sampler check, mixes wrap modes on every test tile. It also made the debug views match the Sample Viewer's, and gave `SceneRenderer` the material buffer's address at construction and the debug view each frame. A second pass that day renamed `base_shader.slang` to `scene.slang` and dropped a planned `--model` flag: the verification assets are viewed through a local change to `BasicApp` instead. A review that day mirrored `DebugView` in the shader as a Slang enum, derived the sampler counts from each enum's `Count`, asserted that a material's texture ids are registered bindless slots, moved `ModelNode` and `ModelData` to `model_data.h`, and gave `Prefab` one `MeshComponent` per mesh. The tasks below describe the result.

**Data:**
- `src/core/material_data.h`, next to `mesh_data.h`:
```cpp
enum class AlphaMode : uint8_t { Opaque, Mask, Blend };
// Count isn't a setting; SamplerManager sizes its sampler table with it
enum class TextureWrap : uint8_t { Repeat, Clamp, Mirror, Count };
enum class TextureFilter : uint8_t { Linear, Nearest, Count };

// A material's texture and its sampler settings; image indexes ModelData::images, -1 for none
struct TextureRef {
  int32_t image = -1;
  TextureWrap wrapU = TextureWrap::Repeat; // glTF wrapS
  TextureWrap wrapV = TextureWrap::Repeat; // glTF wrapT
  TextureFilter filter = TextureFilter::Linear;
};

// glTF metal-roughness; each factor multiplies its texture. The defaults are the engine's own: a dielectric.
struct MaterialData {
  std::string name;
  LinearColor baseColor = LinearColor::White;
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3 emissive{0.0f}; // As authored (factor x emissive_strength) until Phase 4 gives it units
  float normalScale = 1.0f;
  float occlusionStrength = 1.0f;
  float alphaCutoff = 0.5f;
  AlphaMode alphaMode = AlphaMode::Opaque;
  bool doubleSided = false;
  TextureRef baseColorTexture{};         // sRGB
  TextureRef metallicRoughnessTexture{}; // Linear; G = roughness, B = metallic
  TextureRef normalTexture{};            // Linear
  TextureRef occlusionTexture{};         // Linear; R
  TextureRef emissiveTexture{};          // sRGB
};
```
- `ModelData` gains `std::vector<ImageData> images` and `std::vector<MaterialData> materials`, and moves with `ModelNode` to `src/core/model_data.h`, so `mesh_data.h` keeps only the geometry. `SubmeshData` gains `int32_t materialIndex = -1` (into `ModelData::materials`; -1 for the default material).
- `gpu::Material` (72 bytes), mirrored in Slang and read through a pointer:
```cpp
struct Material {
  LinearColor baseColor = LinearColor::White;
  Vec3 emissive{0.0f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float normalScale = 1.0f;
  float occlusionStrength = 1.0f;
  float alphaCutoff = 0.5f;
  // Each texture packs its bindless index (low 16 bits) and its sampler index (high 16 bits)
  uint32_t baseColorTexture = 0;
  uint32_t metallicRoughnessTexture = 0;
  uint32_t normalTexture = 0;
  uint32_t occlusionTexture = 0;
  uint32_t emissiveTexture = 0;
  uint32_t flags = 0; // Bit 0: has a normal texture
};
```
- Struct changes (sizes and offsets checked with slangc on Oct 24):
  - `InstanceData`: `textureIndex` and `samplerIndex` become one `uint32_t materialIndex` (148 bytes).
  - `ScenePushConstants` gains `VkDeviceAddress materials` (32 bytes).
  - `GlobalUniforms` gains `uint32_t debugView` (68 bytes).
  - `TonemapPushConstants` gains `uint32_t applyCurve` (12 bytes).
- `enum class DebugView : uint32_t { Lit, BaseColor, Normal, Metallic, Roughness, Occlusion, Emissive, Uv, Count }` in `gpu_types.h`, because the shader reads its values. `scene.slang` mirrors it as a Slang enum.

**Tasks:**
1. **Rename `shaders/base_shader.slang` to `shaders/scene.slang`**, first and in its own commit, so git follows the file through the rewrite. `vk_utils::kDefaultShader` becomes `kSceneShader = "scene"`, and the comments and the path test that name the file (`gpu_types.h`, `tonemap.slang`, `core_test.cpp`) follow.

   *Why:* the shader is named for what it draws, like `SceneRenderer`, whose `ScenePushConstants` it reads. The shadow cascades and sensor views of later phases draw the same scene from other views, so the name stays true.
2. **glTF importer** (`src/importers/gltf_importer.cpp`):
   - **Materials** come from `cgltf_material`, in file order, so `ModelData::materials[i]` is glTF material `i`:
     - the `pbr_metallic_roughness` fields;
     - `normal_texture.scale`;
     - `occlusion_texture.scale` (the occlusion strength);
     - `emissive_factor`, times `emissive_strength.emissive_strength` when `has_emissive_strength`;
     - `alpha_mode`, `alpha_cutoff`, `double_sided`.

     cgltf already fills in glTF's defaults (base color 1, metallic 1, roughness 1, cutoff 0.5), also when `pbrMetallicRoughness` is missing. It sets a texture's scale only along with the texture, so a material without a normal or occlusion texture keeps `MaterialData`'s 1.
   - **Primitives without a material** get glTF's default material (white, metallic 1, roughness 1), appended once after the file's own.
   - **Texture references:**
     - A texture view without a texture, or whose texture has no core image (`texture->image` is null when only KHR_texture_basisu or EXT_texture_webp gives one), has no image.
     - `wrapU` and `wrapV` come from the sampler's `wrap_s` and `wrap_t`; `filter` is Nearest when `mag_filter == cgltf_filter_type_nearest`. A texture without a sampler keeps glTF's defaults: repeat and linear.
     - A texture view with `texcoord != 0` is dropped with a warning; only TEXCOORD_0 is imported.
     - `has_transform` (KHR_texture_transform) is ignored with a warning.
   - **Images:**
     - Imported per use, not per file: base color and emissive use an image as sRGB; metallic-roughness, normal, and occlusion as linear. Each glTF image is decoded once, and an image used both ways gets a second `ModelData` entry, a copy with the other `srgb` flag. Images no material uses aren't imported.
     - Decode GLB and buffer-view images with `ImageImporter::importFromMemory({cgltf_buffer_view_data(view), view->size})`.
     - Decode an external URI with `cgltf_decode_uri` (on a copy) and resolve it against the glTF file's folder: `std::filesystem::path(iPath).parent_path() / uri`. `ImageImporter::importFromFile` resolves that like `iPath`, relative to the content folder unless it's absolute.
     - `data:` URIs, URIs with a scheme (`://`), and images stb can't decode (KTX2, WebP) log a warning naming the material and the slot, and the texture is dropped.
   - Each submesh's `materialIndex` comes from `primitive.material` (`cgltf_material_index`).
   - `KHR_materials_emissive_strength` joins `kSupportedRequiredExtensions`, since the importer honors it now.
   - The summary log line adds material and image counts. Update the comment in the "Requiring An Unsupported Extension" test: materials are imported now, but KHR_texture_transform isn't honored, so a file that requires it is still rejected. Add the dropped textures to the README's **Errors** convention.
3. **Samplers** (`src/renderer/sampler_manager.{h,cpp}`):
   - `SamplerType` (`rhi_types.h`) goes away. `SamplerManager` holds one sampler per `TextureFilter` × U `TextureWrap` × V `TextureWrap`, 18 in all, then the shadow sampler at `kShadowSamplerIndex` (18).
   - `static constexpr uint32_t getTextureSamplerIndex(iFilter, iWrapU, iWrapV)` returns `(filter * kTextureWrapCount + wrapU) * kTextureWrapCount + wrapV`, with the counts (and `kTextureSamplerCount`) taken from each enum's `Count`. The constructor fills every slot through it, so the order is defined in one place.
   - Linear samplers keep trilinear filtering and 16× anisotropy; nearest ones keep nearest mips and no anisotropy. Debug names spell out the settings, such as `Sampler_Linear_Repeat_Clamp`.
   - Remove the unused `get(SamplerType)`. `getSamplers()` returns all 19 for the bindless set; update `BindlessManager`'s comment.
4. **`ResourceManager`:**
   - **Material buffer:** device-local, `STORAGE | SHADER_DEVICE_ADDRESS | TRANSFER_DST`, sized for `kMaxMaterials = 4096` (in `utils.h` next to `kMaxSampledImages`) and written through `StagingManager`. It outlives the staging manager, whose shutdown flush may still copy into it.
   - **Default material:** index 0, created in the constructor after the white texture, from `MaterialData{}` (the engine's dielectric). `getDefaultMaterial()` returns it. The destructor releases it before the pipelines.
   - **`std::shared_ptr<Material> createMaterial(const MaterialData& iData, std::span<const uint32_t> iTextureIds = {})`** replaces `createMaterial(iShaderName)`:
     - Each `TextureRef` becomes `iTextureIds[image]` (assert that `image` is in range), or `kFallbackTextureIndex` when it has no image, packed with `SamplerManager::getTextureSamplerIndex(filter, wrapU, wrapV)`. `static_assert` that `kMaxSampledImages` and `kSamplerCount` fit in 16 bits, and assert that each texture id is a registered bindless slot: a larger one would spill into the sampler bits.
     - `flags` bit 0 is set when the normal texture resolves to a texture other than the fallback.
     - The pipeline stays `getOrCreatePipeline()`, which drops its shader-name parameter: nothing sets it once `createMaterial` stops taking one. Until Phase 3's variants it holds one pipeline, `mScenePipeline`.
     - When the buffer is full, log `NE_ERROR` and return the default material.
   - **`getMaterialBufferAddress()`**, for `SceneRenderer`.
5. **`Material`** becomes immutable: `Material(iPipeline, iMaterialIndex, iAlphaMode, iDoubleSided)` and their getters. Remove `setTexture`, `setSampler`, and the texture and sampler getters.
6. **`Prefab`:**
   - creates one texture per `ModelData` image, named after the model and the image index;
   - creates one material per `MaterialData`, passing those texture ids;
   - keeps one `MeshComponent` per mesh, holding the mesh and its submeshes' materials (the default material for `-1`; assert that other indices are in range), and copies it into every node that draws the mesh. `mMaterial` goes away.
7. **`SceneRenderer`:**
   - `SceneRenderer(iGeometryAllocator, iMaterialBufferAddress)`: the buffer never moves, so `Renderer` passes its address once.
   - `DrawCall` carries `materialIndex` instead of the texture and sampler indices; sorting and instancing stay as they are.
   - `record(iCommandBuffer, ioUploadAllocator, iDebugView)` writes the debug view into `GlobalUniforms` and the material buffer's address into the push constants.
8. **`scene.slang`:**
   - Pass `materialIndex` from the vertex shader with `nointerpolation` and fetch `pc.materials[materialIndex]`.
   - Sample every texture through:
     ```slang
     float4 sampleTexture(uint packed, float2 uv) {
       return gTextures[NonUniformResourceIndex(packed & 0xFFFF)].Sample(gSamplers[NonUniformResourceIndex(packed >> 16)], uv);
     }
     ```
   - Put `#pragma warning(disable : 41012)` after `#language`, with a comment. slangc 2026.19 warns "profile implicitly upgraded" whenever a texture or sampler index goes through `NonUniformResourceIndex`, and lists capabilities the module never declares. The SPIR-V adds only `ShaderNonUniform` and `SampledImageArrayNonUniformIndexing` to today's capabilities, which the enabled descriptor-indexing features cover, and validation checks capabilities when the shader module is created.
   - Base color = factor × texture × vertex color × instance tint.
   - Occlusion multiplies only the ambient term: `ao = 1 + strength * (texture.r - 1)`.
   - Emissive is added after lighting.
   - Metallic and roughness are fetched, but only the debug views use them.
9. **Debug views** (see **Decisions**):
   - `Renderer::setDebugView` and `getDebugView`, and `Application::getRenderer()`.
   - A "Debug View" combo in `MainUI`'s Diagnostics window; its names array is checked against `DebugView::Count`.
   - The scene shader writes the chosen channel instead of the lit color:
     - BaseColor and Emissive are colors, written linear like the lit color;
     - Normal (`N * 0.5 + 0.5`), Metallic (B × factor), Roughness (G × factor), Occlusion (`ao`), and Uv (`float3(uv, 0)`) are data, written through `srgbToLinear`, so the display's sRGB encode puts the stored value on screen.
   - The tonemapper applies PBR Neutral only in the Lit view: `Tonemapper::record` gains `iApplyCurve`, pushed as `applyCurve`.
10. **`BasicApp`:** each cube gets a `MaterialData` with `baseColorTexture.image = 0`, passed with `iTextureIds = {checkerId}`. The second cube's texture uses `filter = Nearest`.
11. **Tests:**
    - importer, DamagedHelmet:
      - 1 material and 5 images;
      - the base color and emissive images are sRGB, the other three linear;
      - metallic and roughness factors are 1, emissive is (1, 1, 1);
      - its submesh uses material 0.
    - importer, Box.gltf: material "Red" has base color (0.8, 0, 0, 1), metallic 0, roughness 1, and no textures.
    - importer: a file that requires KHR_materials_emissive_strength imports.
    - importer, a triangle from `writeTriangleGltf` (which gains a parameter for primitive fields such as `"material": 0`), with `uv_checker.png` copied next to it:
      - a base color texture whose sampler is nearest, clamp S, mirror T imports as `filter = Nearest`, `wrapU = Clamp`, `wrapV = Mirror`;
      - the same image as the metallic-roughness texture is imported a second time, linear;
      - a normal texture on TEXCOORD_1 is dropped;
      - a triangle without a material gets glTF's default material (metallic 1, roughness 1) as material 0.
    - scene: a prefab whose two submeshes use two materials gives each submesh its own material, and a submesh with `-1` gets the default material.
    - The test "A Frame's Uploads Can Outgrow One Block" takes `getDefaultMaterial()`.

**Done when:**
- Under the old light, DamagedHelmet shows its albedo, its emissive as authored, and ambient darkened by its occlusion map.
- The cubes still show the linear and nearest checkers.
- Each debug view of the helmet shows what the Sample Viewer's debug channel of the same name shows (Time Scale 0x stops the spin). Normal follows the engine's axes, not glTF's: R, G, and B grow along +X, +Y, and +Z, so the helmet's top is blue. The Metallic view shows the helmet's metal parts in white.
- TextureSettingsTest, loaded in place of the helmet, shows its clamp, repeat, and mirror tiles' green results, as in the Sample Viewer. The double-sided tile waits for Phase 3.

**Pitfalls:**
- `NonUniformResourceIndex` is required. One instanced draw holds instances with different materials; today's two cubes already use different samplers. Indexing descriptor arrays non-uniformly without it is undefined behavior that only shows up on some GPUs.
- Color space is decided per use, not per file. An `_SRGB` normal or metallic-roughness texture breaks shading silently.
- glTF metallic-roughness channels: G is roughness and B is metallic. Occlusion is R, and it is often the same image (an ORM texture).
- glTF's default material is fully metallic; the engine's own default is a dielectric.
- Mask and blend materials render as opaque until Phase 3.
- Set the normal-texture flag from the resolved texture id, not from `TextureRef::image`. A texture that fell back to white (past the size limit, or no bindless slot left) would otherwise become a (1, 1, 1) normal map in Phase 3.
- The helmet's five 2048² JPEGs take about 1.2 s to decode and mip in Debug (0.5 s decoding, 0.7 s mips; measured Oct 24), so startup and every test that imports the helmet get slower. That's expected, and nothing needs optimizing yet.

---

## Phase 3: Tangents, Normal Maps & Alpha
**Status:** not started

**Goal:**
- Normal maps shade correctly, with tangents from glTF or MikkTSpace.
- Alpha mask and alpha blend work.
- Double-sided materials render both sides.

**Tasks:**
1. **Vertex format:**
   - `gpu::Vertex` gains `Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}`, making it 64 bytes. Update the `static_assert` and the Slang struct.
   - `SubmeshData` gains `std::vector<Vec4> tangents`, and `GeometryAllocator::stageMesh` writes it.
2. **glTF importer:**
   - Read `TANGENT`; only VEC4 is allowed, so add it to `hasAllowedType`. Convert xyz with `toEngineAxes` and keep w.
   - A primitive without `NORMAL` gets flat normals:
     - expand to one vertex per index (positions, UVs, colors);
     - use the face normals;
     - set indices to 0..n−1;
     - drop its tangents.
3. **Vendor MikkTSpace** in `third_party/mikktspace/`:
   - `mikktspace.c` and `mikktspace.h` from github.com/mmikk/MikkTSpace (zlib license, in the header);
   - a `CMakeLists.txt` that records the source and commit and builds it as C, like the other vendored folders;
   - link it to the engine.
4. **`MeshUtils`** (`src/renderer/mesh_utils.{h,cpp}`):
   - **`bool generateTangents(SubmeshData& ioSubmesh)`** needs normals and UVs. It:
     1. expands the submesh to one vertex per index;
     2. runs `genTangSpaceDefault`;
     3. negates each `w`;
     4. calls `weldVertices`.

     It returns false, and logs why, when it can't.
   - **`void weldVertices(SubmeshData& ioSubmesh)`** merges vertices whose position, normal, UV, color, and tangent are bitwise equal, and rebuilds the indices. Mesh Formats reuses it later.
5. **`ResourceManager::createMesh`:** after `validate()`, generate tangents on a copy of each submesh that has normals and UVs but no tangents.
6. **Shader, normal mapping** (when `flags & 1`):
   - Transform the tangent with the model matrix, not the normal matrix, then Gram-Schmidt it against N.
   - `B = cross(N, T) * tangent.w`.
   - `n = texture * 2 - 1`, then `n.xy *= normalScale`.
   - `N = normalize(T * n.x + B * n.y + N * n.z)`.
   - On back faces (`SV_IsFrontFace` false), flip N and the tangent frame before shading.
   - Add a `Tangent` debug view.
7. **Pipelines:**
   - `Pipeline::Config` gains `bool alphaBlend` (src alpha / one-minus-src-alpha) and `std::string fragmentEntry = "fragMain"`.
   - `ResourceManager::getOrCreatePipeline(alphaMode, doubleSided)` caches by both:

     | Material | Fragment entry | Other state |
     |---|---|---|
     | Opaque | `fragMain` | — |
     | Mask | `fragMainMasked` (discards below `alphaCutoff`) | — |
     | Blend | `fragMain` | `alphaBlend`, `DepthMode::ReadOnly` |
     | Double-sided (any of the above) | — | `VK_CULL_MODE_NONE` |

   - `Material` takes its pipeline from these variants.
8. **`SceneRenderer`:**
   - Opaque and mask draws work as today: sorted by pipeline, then submesh, and instanced.
   - Blend draws come afterwards, sorted back to front by the distance from the camera to the entity's world position.
   - Each blend draw gets its own indirect command (instance count 1). The multi-draw splits only where the pipeline changes.
9. **Tests:**
   - `generateTangents` on `createBoxMeshData`: 24 vertices after welding. Every tangent is unit length and perpendicular to its normal, and every `w` is ±1.
   - importer: a triangle without `NORMAL` gets 3 flat normals equal to its face normal.

**Done when:**
- The helmet's panel seams and dents are lit from the light's side; the bumps don't look inverted.
- In glTF-Sample-Assets' NormalTangentMirrorTest, every tile looks the same.
- AlphaBlendModeTest's opaque, mask, and blend rows look as they do in the Khronos glTF Sample Viewer.
- A double-sided plane is visible from behind, and TextureSettingsTest's double-sided tile looks as it does in the Sample Viewer.

**Pitfalls:**
- Negate MikkTSpace's `w`, or equivalently run it on `1 − v`. If you forget, the bumps look inverted. That's easy to miss on the helmet and obvious on NormalTangentMirrorTest.
- MikkTSpace's output is one vertex per index. Weld it again, or vertex counts triple.
- Mirrored instances (negative scale determinant) flip the handedness too. That's ignored for now; the importer already warns about mirrored nodes.
- Blend draws can't be merged into instanced draws. Blended pixels need the opaque depth but must not write depth.
- Put `discard` only in the masked entry point, so opaque draws keep early depth testing.

---

## Phase 4: Lights, Exposure & PBR Shading
**Status:** not started

**Goal:** Physically based shading:
- the glTF metal-roughness BRDF, lit by light components in physical units;
- a hemisphere ambient with a specular term;
- camera exposure.

The light fixed in the shader goes away.

**Data:**
- `src/components/light_component.h`:
```cpp
struct LightComponent {
  enum class Type : uint8_t { Directional, Point, Spot };
  Type type = Type::Directional;
  LinearColor color = LinearColor::White;
  float intensity = 1.0f;                   // Directional: lux. Point and spot: candela
  float range = 0.0f;                       // Point and spot: where the light fades out; 0 for inverse-square only
  float innerConeAngle = 0.0f;              // Spot, radians
  float outerConeAngle = math::kPi / 4.0f; // Spot, radians
};
```
- `src/components/environment_component.h`, at most one per scene: `LinearColor skyColor`, `float skyIntensity` (nits), `LinearColor groundColor`, `float groundIntensity` (nits).
- `CameraComponent` gains:
  - `aperture = 16.0f` (f-number);
  - `shutterSpeed = 1.0f / 125.0f` (seconds);
  - `iso = 100.0f`;
  - `getEv100()` and `getExposure()`.
- `gpu::Light` (56 bytes) and the new `GlobalUniforms` (120 bytes):
```cpp
struct Light {
  Vec3 position;           // Point and spot
  float range = 0.0f;
  Vec3 direction;          // Where the light travels: its entity's +X
  float intensity = 0.0f;  // Lux or candela
  Vec3 color;              // Linear
  uint32_t type = 0;       // LightComponent::Type
  float spotScale = 0.0f;  // 1 / max(cos(inner) - cos(outer), 0.001)
  float spotOffset = 0.0f; // -cos(outer) * spotScale
};

struct GlobalUniforms {
  Mat4 viewProj;
  VkDeviceAddress lights = 0; // Offset 64, so the address stays 8-byte aligned
  Vec3 cameraPosition;
  float exposure = 1.0f;
  Vec3 skyRadiance;           // Nits: color x intensity
  uint32_t lightCount = 0;
  Vec3 groundRadiance;
  uint32_t debugView = 0;
};
```

**Tasks:**
1. Add `math::log2` and `math::exp2` wrappers to `math_utils.h`, like `math::sqrt`.
2. **Exposure** (Lagarde & de Rousiers, "Moving Frostbite to PBR"):
```cpp
// EV100 = log2(N^2 / t * 100 / S)
float getEv100() const { return math::log2(aperture * aperture / shutterSpeed * 100.0f / iso); }
float getExposure() const { return 1.0f / (1.2f * math::exp2(getEv100())); }
```
3. **`SceneRenderer`:**
   - gathers `TransformComponent` + `LightComponent` in `prepare` and uploads them in `record` (position = world translation, direction = normalized world +X), precomputing the spot scale and offset;
   - reads the `EnvironmentComponent`; with none, the ambient is black;
   - fills `GlobalUniforms` from the primary camera (position, exposure).
4. **glTF importer:** multiply emissive by `kGltfEmissiveNits = 1000.0f`.
5. **Shader** (`scene.slang`):
   - Remove the fixed light and ambient.
   - Pass the world position from the vertex shader.
   - N is the normal-mapped normal; `V = normalize(cameraPosition - worldPos)`; `NdotV = max(dot(N, V), 1e-4)`.
   - Clamp perceptual roughness to at least 0.045.

   The math:
```slang
float3 F0 = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic);
float3 diffuseColor = base.rgb * (1.0 - metallic);
float a = roughness * roughness;
float a2 = a * a;

// Each light gives L (unit vector toward the light) and E (illuminance at the point, in lux):
//   Directional: L = -light.direction; E = color * intensity
//   Point / spot: d2 = max(dot(toLight, toLight), 1e-4); L = toLight * rsqrt(d2); att = 1 / d2
//     range > 0: r = d2 / (range * range); att *= saturate(1 - r * r)               (KHR_lights_punctual)
//     spot:      t = saturate(dot(light.direction, -L) * spotScale + spotOffset); att *= t * t
//     E = color * intensity * att
float3 H = normalize(V + L);
float NdotL = saturate(dot(N, L));
float NdotH = saturate(dot(N, H));
float VdotH = saturate(dot(V, H));
float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
float D = a2 / (kPi * d * d);
float Vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2) + NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2) + 1e-7);
float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
radiance += ((1.0 - F) * diffuseColor / kPi + F * D * Vis) * E * NdotL;

// Hemisphere ambient, world +Z up
float3 hemisphere(float3 dir) { return lerp(groundRadiance, skyRadiance, saturate(dir.z * 0.5 + 0.5)); }
float3 R = normalize(lerp(reflect(-V, N), N, a));
radiance += (diffuseColor * hemisphere(N) + envBrdfApprox(F0, roughness, NdotV) * hemisphere(R)) * ao;

radiance += emissive;
return float4(min(radiance * exposure, 64000.0), base.a);

// Karis, "Physically Based Shading on Mobile" (2014)
float3 envBrdfApprox(float3 F0, float roughness, float NdotV) {
  const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
  const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
  float4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
  float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
  return F0 * AB.x + AB.y;
}
```
6. **`BasicApp`** starting values (tune by eye):
   - a directional light of 1000 lux, pitched down about 50° (in REP-103, positive pitch tilts +X down);
   - an environment: sky (0.65, 0.8, 1.0) × 150 nits, ground (0.5, 0.45, 0.4) × 50 nits;
   - the camera at f/4, 1/60 s, ISO 400 (EV100 ≈ 7.9).
7. **UI:**
   - Camera Settings: aperture, shutter, and ISO, plus the resulting EV100.
   - A Lighting window: sun intensity, color, and direction; sky and ground.
8. **Tests** (`camera` suite):
   - f/16, 1/100 s, ISO 100 gives EV100 ≈ 14.64 and exposure ≈ 3.26e-5.
   - Doubling the shutter time doubles the exposure.

**Done when:**
- The helmet's metal parts reflect the sky instead of going black.
- Highlights shrink as roughness drops; MetalRoughSpheresNoTextures shows a clean grid.
- One stop (shutter × 2) doubles the brightness.
- Doubling every light and sky intensity while halving the shutter time gives the same image.
- There are no black or white speckles (NaN or inf).
- The helmet's emissive glows at the indoor exposure.

**Pitfalls:**
- Exposure is applied once, in the scene shader. The tonemap must not apply it again.
- Pre-exposure is what keeps fp16 from overflowing. `min(..., 64000)` catches the rest, such as tiny roughness under a bright light.
- A light shines along +X, so the directional light's `L` is `-direction`.
- With metallic = 1 the diffuse term is zero. Without the hemisphere's specular term, metals turn black, so don't drop it.
- Physical units span a huge range. A 100,000 lux sun at f/16, 1/125 s, ISO 100 looks normal, and so does 1000 lux at f/4, 1/60 s, ISO 400. Pairing one setup's lights with the other's exposure looks all black or all white; that's correct, not a bug.

---

## Phase 5: Shadows
**Status:** not started

**Goal:** The directional light casts stable cascaded shadows. First, the scene's draws are gathered once per frame and recorded per view; Render Views reuses this for sensors.

**Part A: per-view drawing** (no visual change)
1. Split `SceneRenderer::record` per view:
   - `prepare(ioRegistry)` (from Phase 1), once per frame before `beginFrame`: gathers and sorts draws and lights. The camera lookup moves to `Renderer`.
   - `upload(ioUploadAllocator)`, once per frame:
     - uploads instance data, draw infos, and indirect commands;
     - builds the batch list.
   - `recordView(iCommandBuffer, ioUploadAllocator, iView)`, once per view, into the pass `Renderer` opened:
     - uploads that view's `GlobalUniforms`;
     - binds each batch's pipeline;
     - issues the multi-draws.
2. A `View` struct holds the view-projection matrix, camera position, exposure, and pass kind (color or depth-only).
3. `Renderer` finds the primary camera, calls `prepare` and `upload` once, then `recordView` for the camera.

**Part B: cascaded shadow maps**
1. `LightComponent` gains `bool castShadows = false`. The first directional light that casts shadows gets cascades.
2. **Shadow atlas:**
   - one `Image`: 4096×4096 `VK_FORMAT_D32_SFLOAT`, `DEPTH_STENCIL_ATTACHMENT | SAMPLED`;
   - registered once in the bindless table; its size never changes, so it needs no slot updates;
   - four 2048² tiles.
3. **Cascades:**
   - Splits run from the camera's near plane to `shadowDistance` (60 m, a renderer setting), using the practical split scheme (λ = 0.8, blending log and uniform splits).
   - Per cascade:
     1. Take the bounding sphere of the frustum slice. Round its radius up so it doesn't change from frame to frame.
     2. Light view: the light's world rotation, placed at `center - forward * (radius + casterMargin)`.
     3. Projection: reverse-Z orthographic, 2 × radius wide. Use `CameraComponent`'s orthographic math with aspect 1; near plane 0.01, far plane 2 × radius + casterMargin.
     4. Snap: project the world origin, round it to whole shadow-map texels, and add the difference to the projection's translation.
4. **Shadow pipelines:** depth-only variants (no color attachment, D32 depth, depth bias enabled).
   - Opaque materials use an empty `fragmentEntry`, which means a vertex-only pipeline.
   - Mask materials use `fragMainMasked`.
   - Blend materials don't cast shadows.
5. `Renderer` opens one depth-only pass on the atlas, cleared once, and renders each cascade with `recordView`, the viewport and scissor set to its tile.
6. **Shading:**
   - Upload a `gpu::ShadowData` per frame (4 view-projection matrices, split depths, atlas texture index, texel size) and put its address in `GlobalUniforms`.
   - Pick the cascade from view depth.
   - Offset the world position along N by about 1–2 texels.
   - 3×3 PCF with `SampleCmpLevelZero`, through the shadow sampler (`SamplerManager::kShadowSamplerIndex`). Declare `SamplerComparisonState gShadowSamplers[]` on binding 0, next to `gSamplers`.
   - Clamp the taps inside the tile.
   - Only the directional light's direct term is shadowed.
7. **`BasicApp`:**
   - a ground plane under the objects: a flat box from `MeshUtils::createBoxMeshData`, light grey, roughness 0.8;
   - the light casts shadows;
   - a `Cascades` debug view tints each cascade.

**Done when:**
- The helmet and cubes cast shadows on the ground.
- There's no shadow acne and no detached shadows at the default bias.
- Shadows don't shimmer while the camera moves or rotates.
- The cascade debug view shows four bands.

**Pitfalls:**
- Reverse-Z flips the depth-bias signs: the bias must push depth toward 0, so the constant and slope factors are negative.
- A sampled depth view must have only the depth aspect. D32_SFLOAT has no stencil, so the atlas avoids the problem.
- The `Shadow` sampler compares GREATER_OR_EQUAL. A fragment is lit when its light-space depth is at least the stored occluder depth (in reverse-Z, larger means closer to the light).
- Cascades depend on the camera, so every sensor view needs its own. Render Views decides whether to share or reduce them.
- Spot and point light shadows come after this phase: atlas tiles, six of them for a point light.

---

## Phase 6: Image-Based Lighting
**Status:** not started

**Goal:** An HDR environment map lights and reflects the scene and is drawn as the sky. The hemisphere stays as the fallback when there's no environment map.

**Tasks:**
1. **Image infrastructure:**
   - `Image::Config` gains `arrayLayers`, `VkImageCreateFlags flags` (`CUBE_COMPATIBLE`), and `viewType` (`CUBE`).
   - Extra 2D views, one per face and mip, to render into.
   - Mip generation with `vkCmdBlitImage`; linear blits of `R16G16B16A16_SFLOAT` are mandatory.
2. **HDR images:**
   - `ImageImporter::importHdrFromFile` loads Radiance `.hdr` files with `stbi_loadf` as float RGBA.
   - Convert them on the CPU to half floats (`R16G16B16A16_SFLOAT`), because linear filtering of `R32G32B32A32_SFLOAT` isn't guaranteed. This needs a small float → half conversion in `core/math`.
3. **Assets:** a CC0 HDRI from Poly Haven (1k or 2k `.hdr`). **Decide first:** either build the planned download manifest (URL + SHA-256 into a gitignored cache, cached in CI) or commit one file of 2 MB or less.
4. **Precompute at load** (graphics passes, one fullscreen triangle per face and mip):
   - equirectangular map → 512² environment cube with full mips;
   - prefiltered specular cube, 256² with 6 mips; mip m holds roughness m / 5. Use GGX importance sampling that reads the environment's mips by sample density (filtered importance sampling, which avoids fireflies);
   - SH9 diffuse irradiance, computed on the CPU from the equirectangular pixels and weighted by solid angle.
5. **Bindless:** register cube views in binding 1 like 2D ones, and declare `TextureCube gCubeTextures[]` on binding 1 next to `gTextures`.
6. **`EnvironmentComponent`** gains the cube texture index, the SH coefficients, and `intensity` (nits per unit of the map).
7. **Shading:** when an environment map is set, it replaces the hemisphere:
   - diffuse ambient = SH(N) × diffuseColor;
   - specular = the prefiltered cube sampled at mip `roughness * 5` × `envBrdfApprox`;
   - both multiplied by intensity and ao.
8. **Sky:** after the opaque draws, a fullscreen triangle at depth 0 (reverse-Z far):
   - depth test GREATER_OR_EQUAL, no depth writes;
   - direction from the inverse view-projection;
   - color = environment cube × intensity × exposure.

**Done when:**
- The helmet's metal reflects the environment.
- Rough surfaces show a blurred reflection.
- Diffuse lighting takes on the environment's color.
- The sky shows behind the scene, without seams at cube edges.

**Pitfalls:**
- An HDRI with a visible sun lights the scene twice when a directional light also stands in for the sun. Pick sunless HDRIs, or keep the sun out of the map.
- HDRIs come in arbitrary units. Calibrate `intensity` against the directional light; a sunny sky is a few thousand nits.
- Equirectangular latitude is measured from +Z (engine up), not +Y as most sample code assumes.

---

## Verification Assets
Download these for manual checks, and don't commit them; `content/` stays small. To view one, load it in place of the helmet in a local change to `BasicApp`, and don't commit that either.
- **Khronos glTF Sample Viewer** (github.khronos.org/glTF-Sample-Viewer-Release): the reference renderer, with a PBR Neutral tonemapper option.
- **Models from github.com/KhronosGroup/glTF-Sample-Assets:**

  | Model | Checks | Phase |
  |---|---|---|
  | TextureSettingsTest | samplers (per-axis wrap); double-sided | 2, 3 |
  | NormalTangentMirrorTest, NormalTangentTest | tangents and normal maps | 3 |
  | AlphaBlendModeTest | alpha modes | 3 |
  | MetalRoughSpheresNoTextures | the BRDF | 4 |

## Later (Outside This Plan)
- **Anti-aliasing:** MSAA on the scene target suits forward rendering; decide before Render Views. An entity-ID target can't be averaged, so resolve it with sample zero.
- **Entity-ID attachment** for segmentation sensors and picking.
- **Runtime material edits** (domain randomization): add a barrier before overwriting a slot, or upload materials per frame.
- **Spot and point light shadows.**
- **More glTF features:** TEXCOORD_1, KHR_texture_transform, embedded (`data:`) images, KHR_lights_punctual, the KHR_materials_* extensions.
- **Shading quality:** specular anti-aliasing and multiple-scattering energy compensation.
- **Clustered lighting**, if light counts grow.
- **Render graph:** see Decisions for when to revisit it.
