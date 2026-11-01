# Rendering Plan

Physically based rendering for Null, split into seven phases; Phase 6, the environment BRDF table, was added on Nov 1. Each phase leaves the engine working and can be checked on its own. This file merges the stage 1 plan from the "PBR rendering next steps" session (2026-10-15) with the plan review of 2026-10-18. The README's Rendering section lists the same phases.

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
- **Ownership:** `Application` holds the `Window`, the `Renderer`, and the `Registry`, and runs the loop and the UI hooks. `Renderer` owns everything on the GPU side: the instance, the device, `FrameManager`, `ResourceManager`, `ImGuiManager`, the classes that draw, and the render targets. *Why:* the app holds rendering API objects, not their implementation, so it never has to keep two of them in agreement (such as the formats `SceneRenderer`'s pipelines and the targets share), and apps built on the core library don't repeat the setup.
- **No render graph.** `Renderer` is the only place that orders passes; a pass is one `vkCmdBeginRendering` scope, as in Vulkan. It records every barrier, opens every pass (attachments, load ops, clear values, viewport), and hands the classes that draw into a pass their per-frame resources (the command buffer, the frame's upload allocator); those classes only draw. `Image` builds its own barriers from the layout, access, and stage it already tracks, and back-to-back barriers go out together in one `vk_utils::pipelineBarrier`. *Why:* the frame is a fixed sequence, and later a loop over views. One pass can hold several draws (the tonemap and the UI; later the shadow cascades in one atlas), so a pass can't belong to what draws into it. Revisit only if the frame becomes data-driven (user-defined sensor pipelines), many short-lived post passes pile up, or profiling calls for async compute.
- **No command encoder.** The classes that draw record into the command buffer themselves, with two helpers: `gpu::pushConstants`, which always writes the shared range with its stages, and `Renderer`'s `setViewportAndScissor`, which takes a rectangle so each shadow cascade gets its tile. *Why:* multi-draw indirect keeps a view to a handful of binds, so an encoder that skips repeated binds saves nothing, and its record of what's bound goes stale whenever ImGui's backend binds its own pipeline, index buffer, and set 0. Nothing on the roadmap needs a second graphics API to hide Vulkan behind. Revisit if recording moves onto several threads (Parallel Environments) or compute and utility passes start repeating the same bind, push, and draw steps. Decided Nov 1; it replaces the README's Context-Driven Encoder Pattern & RHI Decoupling item, which Phase 5 was to wait for. GPU Profiling's timestamps and RenderDoc labels go where `Renderer` opens and closes each pass.
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
  - emissive from file formats that give it no unit (glTF now; OBJ, DAE, and MJCF later): multiplied by `kUnitlessEmissiveNits` (1000).

  *Why:* these are KHR_lights_punctual's units. Emissive in those formats has no unit, so the constant is the one place to tune it. It's one value for all of them, so the Emissive debug view, which divides it back out, shows every format's emissive as authored. Godot's materials default to the same 1000 nits. Renamed from `kGltfEmissiveNits` on Nov 1: the value is the engine's, not glTF's.
- **Light direction:** a light shines along its entity's +X (forward), like cameras.
- **Ambient until IBL:** a sky / ground hemisphere with both a diffuse and a specular term (scaled by `envBrdfApprox` until Phase 6's table replaces it). *Why:* without a specular ambient term, metals render black when only punctual lights hit them.
- **Debug views:** a channel view shows what the Khronos Sample Viewer's debug channel shows: data (normals, tangents, metallic, roughness, occlusion, UVs) as stored, base color and emissive as colors, and no PBR Neutral. Once Phase 4 puts emissive in nits, the Emissive view divides it by `kUnitlessEmissiveNits`, so it still shows the authored value. *Why:* the Sample Viewer is the reference renderer, so each view can be checked against it.

### Materials and meshes
- **Material storage:** a device-local buffer of `gpu::Material`, append-only, written at load. The per-instance `colorTint` stays for runtime variation. *Why:* overwriting a slot that an in-flight frame reads needs a barrier, and nothing needs runtime edits before Episodes & Resets.
- **Missing textures:** use the white fallback (index 0). A material flag switches normal mapping on. *Why:* white is neutral for every slot except normals, and it avoids a branch per texture.
- **Samplers:** one immutable sampler per texture filter (linear, nearest) × U wrap × V wrap (repeat, clamp, mirror), 18 in all, then the shadow sampler. A texture's settings index them directly. *Why:* glTF wraps each axis separately, and TextureSettingsTest mixes wrap modes on every test tile. The bindless set layout is update-after-bind, so its samplers count only against the update-after-bind limits, not `maxPerStageDescriptorSamplers` (whose required minimum is 16).
- **Tangents:**
  - use glTF `TANGENT` when present;
  - otherwise the glTF importer generates them with MikkTSpace (vendored `mikktspace.c/.h`), negates `w`, and welds the vertices again, but only for a primitive whose material kept a normal texture;
  - the shader applies them as `mikktspace.h` prescribes: the bitangent is `w * cross(N, T)` per pixel, from the interpolated normal and tangent as they arrive, and only the final normal is normalized.

  *Why:* glTF names MikkTSpace for missing tangents, and MikkTSpace bakers write normal maps that invert exactly this transform. glTF normal maps put +Y toward the top of the image, while UV v points down. glTF calculates tangents only for a material with a normal texture, from that texture's UVs, and only the importer sees a primitive's material; until Oct 28, `ResourceManager::createMesh` generated them for every mesh with UVs.
- **Missing normals:** the glTF importer generates flat normals (one vertex per index), welds back the corners that triangles on one flat face share, and drops the file's tangents. *Why:* glTF requires both, and the weld keeps flat faces from costing three vertices per triangle.
- **Alpha and double-sided:** one pipeline per alpha mode {opaque, mask, blend}, all built up front by `SceneRenderer`. The cull mode is dynamic state (core since Vulkan 1.3), set per batch: back faces for single-sided materials, none for double-sided ones. Draws sort by alpha mode: opaque, mask, then blend, back to front; then single-sided before double-sided. Blend draws that share a submesh and end up next to each other still merge into one instanced draw. *Why:* opaque draws keep early depth testing, and drawing them first fills the depth buffer before the masked draws, whose `discard` keeps them from writing depth early. Blending happens in the right order: Vulkan draws instances in order, so merged blend draws stay back to front. With the cull mode dynamic, sidedness doesn't double the pipelines, and building them up front means none compiles mid-run. This changes the Oct 26 variants, {opaque, mask, blend} × {cull back, cull none}, which `ResourceManager` created on first use.

### Later phases
- **Shadows:** directional cascaded shadow maps (CSM), 4 cascades in one 4096² D32 atlas, stabilized. They come after the per-view refactor. *Why:* an atlas needs no array images, and per-view drawing is shared with Render Views.
- **Environment BRDF:** a 64 × 64 `R16G16_SFLOAT` table, built on the CPU at startup from the light loop's GGX and visibility, replaces `envBrdfApprox`, in its own phase before IBL. *Why:* the fit makes mid-rough metal about a quarter stop too dark and drops rough metal's brightening toward the edges (Phase 6 has the numbers), which a comparison with the Sample Viewer, which reads a table, would show. In its own phase it stays out of IBL's way. Decided Nov 1.
- **IBL:**
  - `.hdr` loaded with stb_image into an RGBA16F cube with prefiltered mips, built with graphics passes;
  - SH9 diffuse computed on the CPU;
  - the specular scaled by Phase 6's environment BRDF table.

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
   - Compile every shader with `-profile sm_5_1+spirv_1_4`. With `-profile spirv_1_4` alone, slangc warns "profile implicitly upgraded" (41012) whenever a texture or sampler index goes through `NonUniformResourceIndex`, a Shader Model 5.1 intrinsic, because that profile lacks the capabilities Slang requires for it. Shader Model 5.1 includes them. Later features, such as wave operations and ray queries, still warn until a shader declares them with `[require(...)]`: Vulkan 1.3 doesn't guarantee them, and Ray-Query LiDAR keeps a fallback for GPUs without ray tracing. `+spirv_1_4` keeps the SPIR-V version, which Slang would otherwise pick from the shader model, so slangc 2026.20 writes the same bytes as with `spirv_1_4` alone. Indexing adds only `ShaderNonUniform` and `SampledImageArrayNonUniformIndexing` to today's capabilities, covered by the enabled descriptor-indexing features.
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
**Status:** done (2026-10-26)

**Goal:**
- Normal maps shade correctly, with tangents from glTF or MikkTSpace.
- Alpha mask and alpha blend work.
- Double-sided materials render both sides.

This is the plan of Oct 15 and 18, checked against the code on Oct 26, after Phase 2. The check builds MikkTSpace as its own C11 library, since its source uses a C11 feature, and replaces the shader's Gram-Schmidt step with the transform `mikktspace.h` prescribes, the exact inverse of what MikkTSpace bakers write. It computes the bitangent before the back-face flip, sorts draws by alpha mode first, and leaves `SceneRenderer::record` as it is: neighboring blend draws of one submesh can share an instanced draw, because instances blend in order. It adds Geometry Normal and Bitangent debug views next to Tangent, names the pipeline variants, adds NormalTangentTest to the checks, and adds tests for the tangent sign, imported tangents, and the pipeline variants. The Khronos viewer generates missing tangents the same way, with MikkTSpace and `w` negated (glTF-Sample-Renderer's `primitive.js`). The implementation that day added `MeshUtils::unweldVertices`, the inverse of `weldVertices`, which tangent generation and the importer's flat normals share. The review of Oct 28 changed these parts; the tasks below still describe Oct 26:
- Tangent generation moved from `ResourceManager::createMesh` into the glTF importer, for primitives whose material kept a normal texture. The importer also welds flat-normal primitives, and `weldVertices` treats −0 as +0, since cross products of axis-aligned edges give both.
- The cull mode became dynamic state. `SceneRenderer` builds one pipeline per alpha mode up front (`Scene_Opaque`, `Scene_Mask`, `Scene_Blend`) and sets the cull mode per batch. `getOrCreatePipeline` is gone, and `Material` holds no pipeline. `ResourceManager` no longer takes the scene formats.
- `Pipeline::Config` lost `cullMode` and the unused `StencilMode`, and its `debugName` became required.
- `MeshUtils` moved to `src/core/`, so the importer doesn't include a renderer header.
- Index counts that aren't whole triangles are rejected, by the importer and by `MeshData::validate()`.
- Enum names come from switches, which `-Wswitch` checks against their enums: `toString()` next to the material enums, and MainUI's names for the debug views.
- Tests that write files get a `TempDirectory` of their own, deleted when the test ends.

**Data:**
- `gpu::Vertex` (64 bytes; offsets 0, 12, 24, 32, 48 and a 64-byte stride, checked with slangc on Oct 26). The vertex pool then holds 1M vertices instead of 1.4M.
```cpp
struct Vertex {
  Vec3 pos{0.0f};
  Vec3 normal{0.0f, 0.0f, 1.0f};
  Vec2 uv{0.0f};
  LinearColor color = LinearColor::White;
  Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz toward +u; cross(normal, xyz) * w points up the image (toward -v)
};
```
- `SubmeshData` gains `std::vector<Vec4> tangents`: engine axes, `w` = ±1 as glTF defines it.
- `AlphaMode` gains `Count`, which sizes `ResourceManager`'s pipeline table.
- `DebugView` becomes `{ Lit, BaseColor, Normal, GeometryNormal, Tangent, Bitangent, Metallic, Roughness, Occlusion, Emissive, Uv, Count }`. Normal now shows the normal-mapped normal.

**Tasks:**
1. **Vendor MikkTSpace**, first and in its own commit, so the phase's diff holds only engine code:
   - `mikktspace.c` and `mikktspace.h` into `third_party/mikktspace/`, from github.com/mmikk/MikkTSpace, master at commit 3e895b4 of 2020-03-25 (its latest). Both files carry the zlib license.
   - A `CMakeLists.txt` that records the source and commit and builds a `STATIC` library in C11 (`project(mikktspace LANGUAGES C)`, `C_STANDARD 11`), with its own flags like glfw. It can't be an INTERFACE library compiled into the engine like cgltf and stb: its `SEdge` union holds an anonymous struct, which C11 allows but C99 and C++ take only as an extension, so the engine's `-Wpedantic` warns.
   - `add_subdirectory` it next to the others and link it to the engine.
2. **Vertex format:** `GeometryAllocator::stageMesh` writes `tangents` like the other attributes, and the default when a submesh has none. `scene.slang`'s `Vertex` mirrors the struct.
3. **glTF importer:**
   - Read `TANGENT`. glTF allows only VEC4, so `hasAllowedType` checks it. Convert xyz with `toEngineAxes` and keep `w`: the axis change is a rotation, so it keeps handedness.
   - A primitive without `NORMAL` gets flat normals, and its tangents are dropped, as glTF requires:
     - expand it to one vertex per index (positions, UVs, colors) with `MeshUtils::unweldVertices`;
     - give each vertex its triangle's counter-clockwise face normal (a zero-area triangle covers no pixels, so any unit normal does);
     - set the indices to 0..n−1.
4. **`MeshUtils`** (`src/renderer/mesh_utils.{h,cpp}`):
   - **`bool generateTangents(SubmeshData& ioSubmesh)`** needs normals and UVs. It:
     1. expands the submesh to one vertex per index;
     2. runs `genTangSpaceDefault`;
     3. stores each tangent with its `w` negated;
     4. calls `weldVertices`.

     It returns false, and logs why, when it can't.
   - **`void unweldVertices(SubmeshData& ioSubmesh)`** gives each index a vertex of its own, in index order, and sets the indices to 0..n−1.
   - **`void weldVertices(SubmeshData& ioSubmesh)`** merges vertices whose position, normal, UV, color, and tangent are bitwise equal, keeps the order they first appear in, and rebuilds the indices. Mesh Formats reuses it later.
5. **`ResourceManager::createMesh`:** after `validate()`, generate tangents on a copy of each submesh that has normals and UVs but no tangents. A submesh whose generation fails keeps the default tangent.
6. **Shader, normal mapping**, the way `mikktspace.h` prescribes:
   - The vertex shader outputs `float4(normalize(mul((float3x3)modelMatrix, tangent.xyz)), tangent.w)`. It uses the model matrix, not the normal matrix: a tangent lies in the surface, and the normal matrix keeps the normal perpendicular to it under non-uniform scale.
   - The fragment shader takes the interpolated N and T as they arrive, unnormalized:
     ```slang
     float3 N = vertIn.normal;
     float3 T = vertIn.tangent.xyz;
     float3 B = vertIn.tangent.w * cross(N, T);
     if (!isFrontFace) { // Only double-sided materials draw back faces
       N = -N;
       T = -T;
       B = -B;
     }
     if ((material.flags & kMaterialHasNormalTexture) != 0) {
       float3 n = sampleTexture(material.normalTexture, vertIn.uv).xyz * 2.0 - 1.0;
       n.xy *= material.normalScale;
       N = n.x * T + n.y * B + n.z * N;
     }
     N = normalize(N);
     ```
   - The debug views write the final N (Normal), the normalized vertex normal (Geometry Normal), and the normalized T and B (Tangent, Bitangent), all after the back-face flip. They match the Sample Viewer's Shading Normal, Geometry Normal, Geometry Tangent, and Geometry Bitangent channels. `MainUI`'s names follow the enum.
7. **Pipelines:**
   - `Pipeline::Config` gains:
     - `std::string fragmentEntry = "fragMain"`;
     - `BlendMode blendMode = BlendMode::Disabled`, with `enum class BlendMode : uint8_t { Disabled, Alpha }` next to `DepthMode` and `StencilMode`. Alpha blends color with `SRC_ALPHA` and `ONE_MINUS_SRC_ALPHA`; the write mask stays RGB, so the alpha factors don't matter;
     - `std::string debugName`, the shader name when empty, so RenderDoc tells the variants apart (`Scene_Mask_DoubleSided`).
   - `ResourceManager::getOrCreatePipeline(iAlphaMode, iDoubleSided)` keeps a table of `AlphaMode::Count` × 2 pipelines, each created on first use:

     | Material | Fragment entry | Other state |
     |---|---|---|
     | Opaque | `fragMain` | — |
     | Mask | `fragMainMasked` | — |
     | Blend | `fragMain` | `BlendMode::Alpha`, `DepthMode::ReadOnly` |
     | Double-sided (any of the above) | — | `VK_CULL_MODE_NONE` |

   - `createMaterial` passes the material's alpha mode and sidedness. `Material` doesn't change.
   - Both entry points share one shading function. `fragMainMasked` first discards when the base color's alpha (factor × texture × vertex color × tint) is below `alphaCutoff`.
8. **`SceneRenderer`:**
   - `prepare` keeps the primary camera's world position.
   - `DrawCall` gains `AlphaMode alphaMode` and `float distanceSq`, from the camera to the entity's world position.
   - Draws sort by alpha mode first: opaque, mask, then blend. Opaque and mask draws then sort by pipeline and submesh, as today; blend draws by `distanceSq`, far to near.
   - `record` doesn't change. It already starts a new multi-draw where the pipeline changes and merges neighbors that share a submesh into one instanced draw. Merged blend draws still blend back to front, because Vulkan draws instances in order.
9. **Tests:**
   - `generateTangents` on `createBoxMeshData`:
     - 24 vertices after welding;
     - every tangent is unit length and perpendicular to its normal, and every `w` is ±1;
     - on every triangle, T points toward +u and `cross(N, T) * w` toward −v, the image's up. This catches a missing `w` negation before any screenshot does.
   - importer, with `writeTriangleGltf` given accessor 4 (VEC3 normals, all (0, 0, 1)) and accessor 5 (VEC4 tangents, all (1, 0, 0, −1)):
     - a triangle without `NORMAL` gets three flat normals equal to its face normal, engine +X (glTF +Z);
     - with `NORMAL`, a `TANGENT` converts to engine axes and keeps `w`: (0, 1, 0, −1);
     - without `NORMAL`, the `TANGENT` is dropped.
   - renderer, "Frames Render With Every Pipeline Variant": one cube per alpha mode × sidedness in front of BasicApp's camera, with base alpha 0.5. Run `runForFrames(3)`, then destroy them. In CI this runs discard, blending, and culling off under validation on lavapipe.

**Done when:**
- In BasicApp only the helmet changes: its panel seams and dents are lit from the light's side, and the bumps don't look inverted.
- In glTF-Sample-Assets' NormalTangentTest (tangents generated) and NormalTangentMirrorTest (tangents from the file), every hemisphere looks the same.
- AlphaBlendModeTest's opaque, mask, and blend rows look as they do in the Khronos glTF Sample Viewer.
- A double-sided plane is visible from behind, and TextureSettingsTest's double-sided tile looks as it does in the Sample Viewer.

**Pitfalls:**
- Negate MikkTSpace's `w`, or equivalently run it on `1 − v`. If you forget, the bumps look inverted. That's easy to miss on the helmet and obvious on NormalTangentTest; the box test catches it first.
- MikkTSpace's output is one vertex per index. Weld it again, or vertex counts triple.
- Compute B before the back-face flip. `cross(-N, -T)` equals `cross(N, T)`, so a B computed after the flip doesn't flip, and back faces get a mirrored bump instead of the reverse one.
- Don't normalize N or T, or Gram-Schmidt T, before building the normal. MikkTSpace bakers invert this formula on the interpolated vectors as they are; normalize once, at the end.
- Mirrored instances (negative scale determinant) flip the handedness too. That's ignored for now; the importer already warns about mirrored nodes.
- Put `discard` only in the masked entry point, so opaque draws keep early depth testing.
- Blended pixels test against the opaque depth but must not write depth.
- A double-sided blend mesh draws its back and front faces in index order, so a closed transparent mesh can blend in the wrong order. Fixing that takes a back-faces-first pass, which nothing needs yet.
- `gpu::DebugView` and the Slang enum list the views in the same order, and nothing checks it. `-Wswitch` flags a view that MainUI doesn't name.
- The helmet has no `TANGENT`, so MikkTSpace runs on it at load: about 0.1 s in Debug (measured Oct 26), next to the 1.2 s of its textures. Welding brings it back to the file's 14,556 vertices.

---

## Phase 4: Lights, Exposure & PBR Shading
**Status:** done (2026-10-29)

**Goal:** Physically based shading:
- the glTF metal-roughness BRDF, lit by light components in physical units;
- a hemisphere ambient with a specular term;
- camera exposure.

The light fixed in the shader goes away.

This is the plan of Oct 15 and 18, checked against the code on Oct 29, after Phase 3. The check made these changes:
- **Every light type takes one path through the shader.** A directional light is stored as a point infinitely far away: a position with w = 0. The vector toward it is then the same at every pixel and has length 1, so dividing by the squared distance changes nothing. A light without a range stores 1 / range² as 0, which turns the range fade off, and a point light stores cone constants that make its cone factor 1. What differs per type is worked out once per light on the CPU, in a switch that `-Wswitch` checks, so `gpu::Light` drops its type (52 bytes instead of 56).
- **The camera's eye is stored the same way.** An orthographic camera's rays are parallel, so every pixel should get the same view direction. The draft used the camera's position, which aims every pixel's view direction at that one point.
- **The Emissive debug view divides by `kUnitlessEmissiveNits`.** Once emissive is in nits it would show white instead of matching the Sample Viewer.
- **Smaller fixes:**
  - a frame without lights uploads no light buffer;
  - the shader skips a light behind the surface before computing H, where a NaN could appear;
  - the "same image" check leaves out the helmet's emission, which doesn't double with the lights.
- **The ground plane moves here from Phase 5,** so point and spot lights have a surface to light.
- **New tests:** every light type, a frame without lights, and emissive in nits.

**Data:**
- `src/components/light_component.h`:
```cpp
// KHR_lights_punctual's light types and units. A light shines along its entity's +X, the way a camera looks.
struct LightComponent {
  enum class Type : uint8_t { Directional, Point, Spot };
  Type type = Type::Directional;
  LinearColor color = LinearColor::White;  // Linear; alpha is ignored
  float intensity = 1.0f;                  // Directional: lux. Point and spot: candela
  float range = 0.0f;                      // Point and spot: where the light fades out; 0 for inverse-square only
  float innerConeAngle = 0.0f;             // Spot, radians: full intensity inside it
  float outerConeAngle = math::kPi / 4.0f; // Spot, radians: no light outside it; above the inner angle, at most pi / 2
};
```
- `src/components/environment_component.h`, at most one per scene:
```cpp
// The light that reaches the scene from far away: until Image-Based Lighting, a sky of one color above the horizon and
// a ground of one color below it
struct EnvironmentComponent {
  LinearColor skyColor = LinearColor::White;
  float skyIntensity = 1.0f; // Nits
  LinearColor groundColor = LinearColor::White;
  float groundIntensity = 1.0f; // Nits
};
```
- `CameraComponent` gains `getEv100()`, `getExposure()`, and, after `primary`:
  ```cpp
  float aperture{16.0f};             // f-number
  float shutterSpeed{1.0f / 125.0f}; // Seconds
  float iso{100.0f};
  ```
  The defaults follow the "sunny 16" rule: EV100 ≈ 15, the exposure for a 100,000 lux sun.
- Emissive is in nits, and the comments on `MaterialData::emissive` and `gpu::Material::emissive` say so. `material_data.h` gains `constexpr float kUnitlessEmissiveNits = 1000.0f`, the nits of an emissive of 1 from a format that gives it no unit, such as glTF (see **Units**).
- `gpu::Light` (52 bytes) and the new `GlobalUniforms` (128 bytes: 124 of fields, then 4 of padding, which Slang adds too). Offsets checked with slangc 2026.20 on Oct 29.
```cpp
// Every type the same way: a directional light is a point infinitely far away (position.w = 0), a light without a range
// has invRangeSq = 0, and a light without a cone has spotScale = 0 and spotOffset = 1
struct Light {
  Vec4 position{0.0f};     // Point and spot: (world position, 1). Directional: (direction toward the light, 0)
  Vec3 intensity{0.0f};    // Linear color x intensity: lux for directional lights, candela for point and spot lights
  float invRangeSq = 0.0f; // 1 / range², 0 without a range
  Vec3 direction{0.0f};    // Where the light travels: its entity's +X
  float spotScale = 0.0f;  // Spot: 1 / max(cos(inner) - cos(outer), 0.001); others: 0
  float spotOffset = 1.0f; // Spot: -cos(outer) * spotScale; others: 1
};

struct GlobalUniforms {
  Mat4 viewProj;
  VkDeviceAddress lights = 0;       // Offset 64, so the address stays 8-byte aligned; 0 when lightCount is 0
  Vec4 eye{0.0f, 0.0f, 0.0f, 1.0f}; // Perspective camera: (position, 1). Orthographic: (backward direction, 0)
  Vec3 skyRadiance{0.0f};           // Nits: color x intensity
  float exposure = 1.0f;
  Vec3 groundRadiance{0.0f};        // Nits: color x intensity
  uint32_t lightCount = 0;
  uint32_t debugView = 0;           // DebugView
};
```

**Tasks:**
1. **Math:** `math::log2` and `math::exp2` wrappers in `math_utils.h`, like `math::sqrt`.
2. **Exposure** (Lagarde & de Rousiers, "Moving Frostbite to PBR"), in `CameraComponent`:
   ```cpp
   // EV100 = log2(N^2 / t * 100 / S)
   float getEv100() const { return math::log2(aperture * aperture / shutterSpeed * 100.0f / iso); }
   // 2^EV100 = N^2 / t * 100 / S, so no exp2(log2(...)) round trip
   float getExposure() const { return 1.0f / (1.2f * (aperture * aperture / shutterSpeed * 100.0f / iso)); }
   ```
   In words: EV100 counts, in stops, how little light the settings let in (each step up halves it). `1.2 × 2^EV100` is the luminance, in nits, that just fills the sensor, and the exposure scales that to 1.0. At BasicApp's f/4, 1/60 s, ISO 400: 4² × 60 × 100 / 400 = 240, so EV100 = log2(240) ≈ 7.9. The sensor fills at 1.2 × 240 = 288 nits, so the exposure is 1/288 ≈ 0.0035, and a white wall of 100 nits comes out at 0.35. The exposure uses the 240 as is: going through EV100 would take its log2 only for exp2 to undo it (Filament's `exposure(N, t, S)` skips it too).
3. **glTF importer:** emissive = factor × emissive strength × `kUnitlessEmissiveNits`.
4. **`SceneRenderer`:**
   - `mViewProj` becomes `std::optional<gpu::GlobalUniforms> mGlobalUniforms`. `prepare` fills it from:
     - the primary camera: the view-projection, the eye, and `getExposure()`. The eye is `(world position, 1)` for a perspective camera and `(-forward, 0)` for an orthographic one, forward being the normalized world +X, as in `getViewMatrix`;
     - the `EnvironmentComponent`: the sky's and the ground's color × intensity. With none, both stay black. Assert that there's at most one, as for primary cameras;
     - the number of lights.
   - `prepare` also gathers every `TransformComponent` + `LightComponent` into `std::vector<gpu::Light> mLights`. `direction` is the normalized world +X and `intensity` is color × intensity; a switch on the type fills the rest:

     | Type | `position` | `invRangeSq` | `spotScale` | `spotOffset` |
     |---|---|---|---|---|
     | Directional | `(-direction, 0)` | 0 | 0 | 1 |
     | Point | `(world position, 1)` | `1 / range²`, or 0 when `range` is 0 | 0 | 1 |
     | Spot | `(world position, 1)` | as for a point light | `1 / max(cos(inner) - cos(outer), 0.001)` | `-cos(outer) * spotScale` |

   - `record` uploads the lights when there are any, then the uniforms, with the lights' address and the debug view. Without lights the address stays 0: an empty vector's `data()` may be null, and `memcpy` from null is undefined even for 0 bytes.
5. **Shader** (`scene.slang`):
   - Mirror `Light` and `GlobalUniforms`, and mirror `kUnitlessEmissiveNits` as `kMaterialHasNormalTexture` is mirrored.
   - Remove the fixed light and ambient.
   - The vertex shader passes the world position, and computes `SV_Position` from it.
   - The Emissive view returns `emissive / kUnitlessEmissiveNits`: the authored value, as the Sample Viewer shows it.
   - For the lit color, N is the normal-mapped normal and `V = normalize(eye.xyz - worldPos * eye.w)`: toward the camera, and for an orthographic camera (w = 0) its backward direction at every pixel. `NdotV = max(dot(N, V), 1e-4)`, and perceptual roughness is clamped to at least 0.045.

   The math (`lights`, `lightCount`, the radiances, and `exposure` are `GlobalUniforms` fields):
   ```slang
   float3 F0 = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic);
   float3 diffuseColor = base.rgb * (1.0 - metallic);
   float a = roughness * roughness;
   float a2 = a * a;
   float3 radiance = float3(0.0, 0.0, 0.0);

   for (uint i = 0; i < lightCount; ++i) {
     Light light = lights[i];
     // Toward the light. For a directional light (w = 0) it's the same at every pixel and has length 1, so 1 / d2 is 1.
     float3 toLight = light.position.xyz - worldPos * light.position.w;
     float d2 = max(dot(toLight, toLight), 1e-4);
     float3 L = toLight * rsqrt(d2);
     float NdotL = dot(N, L);
     if (NdotL <= 0.0) {
       continue; // Behind the surface
     }
     // E: the illuminance (lux) on a surface facing the light, after KHR_lights_punctual's range fade and cone
     float r = d2 * light.invRangeSq; // (distance / range)²
     float cone = saturate(dot(light.direction, -L) * light.spotScale + light.spotOffset);
     float3 E = light.intensity * (saturate(1.0 - r * r) * cone * cone / d2);

     float3 H = normalize(V + L);
     float NdotH = saturate(dot(N, H));
     float VdotH = saturate(dot(V, H));
     float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
     float D = a2 / (kPi * d * d);
     float Vis = 0.5 / (NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2) + NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2));
     float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
     radiance += ((1.0 - F) * diffuseColor / kPi + F * D * Vis) * E * NdotL;
   }

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
   In words: a diffuse surface facing straight up sees only sky, one facing down only ground, and a wall half of each. (N.z + 1) / 2 is the sky's share of what the surface sees, weighted by angle the way diffuse light is, so the blend is exactly the diffuse light of a two-color sky. The specular term reads the same function along the reflection, bent toward N as roughness grows, because a rough surface gathers its reflection from closer to its normal.
6. **`BasicApp`** starting values (tune by eye):
   - the sun: a directional light of 1000 lux, pitched down 50° and yawed −30° (in REP-103, positive pitch tilts +X down and negative yaw turns it right). It shines along (0.56, −0.32, −0.77), so it comes from above the camera's left shoulder;
   - an environment: sky (0.65, 0.8, 1.0) × 150 nits, ground (0.5, 0.45, 0.4) × 50 nits;
   - the camera at f/4, 1/60 s, ISO 400 (EV100 ≈ 7.9);
   - a ground plane, moved here from Phase 5: a flat box from `MeshUtils::createBoxMeshData`, 20 × 20 × 0.1 m, light grey (0.5), roughness 0.8, with its top at z = −1.5, just under the helmet's lowest point (about −1.4);
   - `getSunEntity()` and `getEnvironmentEntity()`, for MainUI.
7. **UI:**
   - Camera Settings: aperture, shutter speed (shown as 1/N s), and ISO, plus the resulting EV100.
   - A Lighting window, hidden at first and toggled from the View menu: the sun's intensity, color, pitch, and yaw, and the sky's and the ground's color and intensity. Like Camera Settings, it checks that the entities still have their components; a renderer test removes them.
8. **Tests:**
   - camera: f/16, 1/100 s, ISO 100 gives EV100 ≈ 14.64 and exposure ≈ 3.26e-5. Doubling the shutter time or the ISO doubles the exposure.
   - renderer, "Frames Render Every Light Type": a directional light, two point lights (one with a range), and a spot light, near BasicApp's cubes. Run `runForFrames(3)`, then destroy them. In CI this runs the light loop under validation on lavapipe.
   - renderer, "Frames Render Without Lights Or An Environment": remove every `LightComponent` and `EnvironmentComponent`, keeping copies, run `runForFrames(3)`, then put them back. BasicApp always has a sun, so no other test renders a frame without a light buffer.
   - importer: the helmet's emissive is `Vec3(kUnitlessEmissiveNits)`. The file that requires KHR_materials_emissive_strength gains a material with emissive factor (1, 0.5, 0) and strength 4, used by its primitive, and imports emissive (4000, 2000, 0).

**Done when:**
- In BasicApp, the sun lights the cubes, the helmet, and the ground from above the camera's left shoulder. Faces turned away from it take the sky's blue from above and the ground's brown from below. The background stays the dark grey clear color, which ignores exposure until Phase 7 draws the sky.
- The helmet's metal parts reflect the sky instead of going black.
- Highlights shrink as roughness drops, and MetalRoughSpheresNoTextures shows a clean grid.
- One stop (shutter time × 2) doubles the scene's HDR values, so mid-tones look one stop brighter.
- Doubling the sun's, the sky's, and the ground's intensities while halving the shutter time gives the same image, except the helmet's emissive parts, which dim by one stop: emission isn't a light, so it doesn't double.
- There are no black or white speckles (NaN or inf).
- The helmet's emissive glows at the indoor exposure (a glTF emissive of 1 is 1000 nits, 3.5 after BasicApp's exposure of 1/288), and the Emissive debug view still matches the Sample Viewer.
- In a local change to BasicApp:
  - a spot light aimed at the ground lights a disc whose edge fades out between the inner and the outer cone;
  - a point light's pool of light dims with distance, and with a range it reaches black at the range;
  - through an orthographic camera, the ground, made glossy, is evenly lit by the sun, with no highlight under the camera.

**Pitfalls:**
- Exposure is applied once, in the scene shader. The tonemap must not apply it again.
- Pre-exposure is what keeps fp16 from overflowing, and `min(..., 64000)` catches the rest. At BasicApp's exposure, a mirror-like highlight (roughness at the 0.045 clamp) under the 1000 lux sun reaches about 67,000, past fp16's maximum of 65,504.
- A light shines along +X, so a directional light's `position` is `-direction`, the direction toward the light.
- With metallic = 1 the diffuse term is zero. Without the hemisphere's specular term, metals turn black, so don't drop it.
- Skip a light with NdotL ≤ 0 before computing H. `normalize(V + L)` is NaN when L = −V (a light shining straight into the camera), and a NaN times a zero NdotL is still NaN.
- Don't use an orthographic camera's position as its eye: every pixel would look from that one point, and flat surfaces would get a highlight that follows the camera.
- Physical units span a huge range. A 100,000 lux sun at f/16, 1/125 s, ISO 100 looks normal, and so does 1000 lux at f/4, 1/60 s, ISO 400. Pairing one setup's lights with the other's exposure looks all black or all white; that's correct, not a bug. The defaults pair a sunny-16 camera with KHR_lights_punctual's intensity of 1, so a new light needs its intensity set.
- Every pixel loops over every light, which is fine for tens of lights. Clustered lighting (see **Later**) is for more.

---

## Phase 5: Shadows
**Status:** done (2026-11-01)

**Goal:** The directional light casts stable cascaded shadows. First, the scene's draws are gathered once per frame and recorded per view; Render Views reuses this for sensors.

This is the plan of Oct 15 and 18, checked against the code on Nov 1, after Phase 4. The check made these changes:
- **No encoder first.** The README's Context-Driven Encoder Pattern & RHI Decoupling no longer comes before this phase (see **No command encoder** in **Decisions**). Part A adds the two helpers that per-view recording uses instead: `gpu::pushConstants`, and a `setViewportAndScissor` that takes a rectangle, for the atlas tiles.
- **`prepare` takes the camera's `SceneView`.** The camera lookup moves to `Renderer`, but blended draws still sort far to near from the camera, now by its eye: the distance from a perspective camera, the depth along the view for an orthographic one. Until now an orthographic camera measured from its position too, so a draw off to the side could count as farther than a deeper one straight ahead.
- **A view also carries its debug view and its shadows,** next to its view-projection, eye, and exposure: those differ per view (sensor views later), while the lights, the environment, and the draws belong to the frame.
- **One upload per frame** for every batch's draw infos, instances, and indirect commands, instead of three per batch. Each batch keeps its offsets, and every view draws from the same data.
- **`Pipeline` learns depth-only pipelines.** Until now every pipeline had a fragment shader and one color attachment. An empty fragment entry now builds a vertex-only pipeline, the blend state declares as many attachments as the pass has color formats (Vulkan requires the counts to match, so none for a depth-only pass), and `Config` gains the depth bias.
- **Masked casters use `fragShadowMasked`,** which only tests alpha. `fragMainMasked` would run the whole shading function, light loop included, just to decide whether to discard.
- **The bounding sphere comes from a formula** of the slice's depths, the field of view, and the aspect ratio, instead of from the slice's corners in world space. It doesn't depend on where the camera is or where it looks, so it's the same every frame without rounding its radius, and it's the smallest sphere around the slice. Orthographic cameras get cascades too: their slices are boxes.
- **The atlas uses the scene's depth format** (`D32_SFLOAT` wherever it's supported), so the shadow pipelines and both depth images share one lookup.
- **`gpu::ShadowData` is spelled out** (see **Data**). `GlobalUniforms` grows to 136 bytes, and Phase 6's note follows.
- **The Cascades debug view tints the lit image,** so unlike the channel views it keeps PBR Neutral.
- **Tests,** which this phase had none of.

The implementation that day changed these parts; the tasks below describe the result:
- **The shadowed light's index moved from `ShadowData` to `GlobalUniforms::shadowLight`,** which holds `gpu::kNoShadowLight` when no light's shadows were drawn. Otherwise the shader tests the shadows pointer against null, which Slang compiles to a 64-bit integer comparison. That needs the `shaderInt64` feature, which the engine doesn't enable, and the validation layer flagged it.
- **Each cascade keeps a border of 4 texels around its sphere** (`ShadowCascades::kBorderTexels`). Snapping moves a tile by up to half a texel, the normal offset reaches 1.5 texels, and the filter 2 more, so lookups near a slice's corners stay inside its tile. The light's eye backs off by half the tile, border included, instead of by the radius.
- **Slang warns that `gShadowSamplers` overlaps `gSamplers`** (warning 39001), so the warning is off for that one declaration, between `#pragma warning(push)` and `#pragma warning(pop)`; any other overlap still warns.
- **BasicApp gained alpha test pieces,** asked for that day: a perforated panel, masked and double-sided, on a plane from the new `MeshUtils::createPlaneMeshData`, standing under the center cube, and a blended glass cube behind the left cube, inside its shadow. The panel's holes show in its shadow, and the glass casts none.
- **`DebugView::Cascades` follows `Lit`,** so the two lit views come first in the menu.

**The frame:**
```
Renderer::render
├─ ResourceManager::flushUploads
├─ swapchain extent changed? → Device::waitIdle, recreate HDR + depth, updateSampledImage
├─ find the primary camera → its SceneView
├─ SceneRenderer::prepare(registry, camera SceneView)  lights, environment, draws gathered + sorted
├─ a directional light casts shadows? → ShadowCascades::compute → 4 depth SceneViews
├─ FrameManager::beginFrame
├─ bind bindless set 0
├─ SceneRenderer::upload                               the lights and every draw's data, once
├─ [shadows] upload gpu::ShadowData → the camera SceneView's shadows
├─ [shadows] atlas → depth attachment
├─ [shadows] shadow pass                               clear; per cascade: viewport + scissor = its tile, recordView
├─ [shadows] atlas → SHADER_READ_ONLY,                 one barrier batch
│  HDR color, depth → attachments
├─ scene pass                                          SceneRenderer::recordView(camera SceneView)
└─ the swapchain pass and the rest as in Phase 1
```

**Part A: per-view drawing** (no visual change, except the blend order under orthographic cameras)
1. `gpu::pushConstants(iCommandBuffer, iLayout, iConstants)` in `gpu_types.h`, next to the range it writes: it passes the range's stages, which Vulkan requires, and `static_assert`s that the struct fits in 128 bytes and is trivially copyable, since Vulkan copies its bytes. `SceneRenderer` and `Tonemapper` push through it.
2. `setViewportAndScissor` in `renderer.cpp` takes a `VkRect2D`.
3. `SceneView`, next to `SceneRenderer` in `scene_renderer.h`. It's a struct of its own, not nested in `SceneRenderer`, so `renderer.h` can declare it ahead without including the header. Part B adds `kind` and `shadows`:
   ```cpp
   // One rendering of the scene: from the camera into the HDR target, or from a light into a shadow map tile
   struct SceneView {
     enum class Kind : uint8_t {
       Color, // Lit, through the scene pipelines
       Depth  // Depth only, through the shadow pipelines; blended draws cast no shadows
     };
     Kind kind = Kind::Color;
     Mat4 viewProj;
     Vec4 eye{0.0f, 0.0f, 0.0f, 1.0f}; // As in gpu::GlobalUniforms
     float exposure = 1.0f;
     gpu::DebugView debugView{};       // Lit
     VkDeviceAddress shadows = 0;      // The gpu::ShadowData a color view shades with; 0 for none
   };
   ```
4. Split `SceneRenderer::record`:
   - `prepare(ioRegistry, iCameraView)`, once per frame before `beginFrame`: gathers the lights, the environment, and the draws, and sorts the draws as before, except that the blended ones go far to near by `iCameraView`'s eye: by distance from a perspective camera, by depth along the view for an orthographic one.
   - `upload(ioUploadAllocator)`, once per frame: uploads the lights, then every draw's data in one go (draw infos, instance data, indirect commands, in that order), and builds the batch list. A batch holds its alpha mode, its sidedness, its first draw, and its draw count, which index the frame's draw infos and indirect commands.
   - `recordView(iCommandBuffer, ioUploadAllocator, iView)`, once per view, into the pass `Renderer` opened: uploads the view's `GlobalUniforms` (the frame's lights and environment with the view's matrix, eye, exposure, debug view, and shadows), binds the index buffer, then per batch binds its pipeline and sets its cull mode where they change, pushes the constants, and issues its multi-draw.
5. `Renderer` finds the primary camera (asserting there's at most one), builds its `SceneView` with the eye as Phase 4 defines it, and calls `prepare`, `upload`, and `recordView`. Without a primary camera the scene pass only clears, as before.

**Part B: cascaded shadow maps**
1. `LightComponent` gains `bool castShadows = false`. The first directional light that casts shadows gets cascades: `SceneRenderer::prepare` notes its index in the light buffer and its world matrix, and `getShadowLight()` returns them.
2. **Shadow atlas,** in `Renderer`:
   - one `Image`: `kShadowAtlasSize` (4096) square, the scene's depth format, `DEPTH_STENCIL_ATTACHMENT | SAMPLED`;
   - registered once in the bindless table; its size never changes, so it needs no slot updates;
   - four 2048² tiles, cascade i in tile (i mod 2, i / 2).
3. **Cascades** (`src/renderer/shadow_cascades.{h,cpp}`, CPU math):
   ```cpp
   // Cascaded shadow maps for one directional light: the camera's view up to a distance, cut into slices by depth, each
   // seen from the light through an orthographic view of its own
   struct ShadowCascades {
     static constexpr uint32_t kCount = 4;
     // Texels around each slice's bounding sphere, for what reaches past it: snapping (half a texel), the normal offset
     // (1.5), and the 3x3 filter's reach (2)
     static constexpr uint32_t kBorderTexels = 4;

     struct Config {
       float distance = 60.0f;      // View depth where shadows end
       float splitLambda = 0.8f;    // 0 spaces the splits evenly, 1 logarithmically
       float casterMargin = 100.0f; // How far toward the light from a slice casters still cast into it
       uint32_t resolution = 2048;  // Texels along a cascade's side
     };

     std::array<Mat4, kCount> viewProj{};     // World → each cascade's clip space, snapped to whole texels
     std::array<float, kCount> splitDepths{}; // The view depth where each cascade ends
     std::array<float, kCount> texelSizes{};  // The world size of each cascade's texels

     // iLightWorld is the light's world matrix; the light shines along its +X. Empty when nothing is left to shadow: the
     // far plane or the distance is at the near plane.
     static std::optional<ShadowCascades> compute(const CameraComponent& iCamera, const Mat4& iCameraWorld,
                                                  float iAspectRatio, const Mat4& iLightWorld, const Config& iConfig);
   };
   ```
   Per cascade:
   1. **Splits** run from the camera's near plane n to f, the shadow distance or the far plane, whichever is nearer. Split i of 4 is λ · n · (f / n)^(i / 4) + (1 − λ) · (n + (f − n) · i / 4), with λ = 0.8 (the practical split scheme). In words: perspective shrinks things with distance, which logarithmic splits match, while the even share keeps the first cascade from shrinking to a sliver in front of the camera.
   2. **Bounding sphere** of the slice between depths d₀ and d₁. Every corner of a perspective slice at depth d lies d · k from the view axis, with k² = tan²(fov / 2) · (1 + aspect²). The center sits on the axis at z = min((d₀ + d₁)(1 + k²) / 2, d₁): there both rings of corners are equally far, or, when that point lies past the far ring, the far ring's own center, whose sphere already holds the near ring. The radius is the distance to a far corner, √((d₁ − z)² + d₁² k²). An orthographic slice is a box: its center is at (d₀ + d₁) / 2 and its radius √(w² + h² + ((d₁ − d₀) / 2)²), for its half width w and half height h.
   3. **Tile:** the sphere spans all but `kBorderTexels` of the tile's texels on each side, so a texel is 2 × radius / (resolution − 2 × 4) wide, and half the tile, border included, is resolution / 2 texels.
   4. **Light view:** the light's world rotation, placed at center − forward · (half the tile + casterMargin), forward being the light's +X. It's built from the light's rotation, not the camera's, so the shadow map's texel grid doesn't turn with the camera.
   5. **Projection:** `CameraComponent`'s reverse-Z orthographic math with aspect 1, as tall as the tile; near plane 0.01, far plane the tile's height + casterMargin.
   6. **Snap:** project the world origin, round where it lands to whole texels of the cascade (resolution / 2 per unit of clip space), and add the difference to the view-projection's translation. With the radius fixed, every world point then lands on the same spot within its texel however the camera moves, so shadow edges don't crawl.

   For BasicApp's camera (45° vertical field of view, near plane 0.1 m, aspect 1.2) and the 60 m distance: k² = tan²(22.5°) · (1 + 1.44) = 0.419, and the splits fall at 3.4, 8.0, 18.7, and 60 m. Cascade 0 runs from 0.1 to 3.4 m: its center is at z = 3.51 · 1.419 / 2 = 2.49 m, and its radius √(0.92² + 3.41² · 0.419) = 2.39 m, so the 2040 texels inside its border span 4.78 m, 2.3 mm each. Cascades 1 and 2 center on their far rings (radii 5.2 and 12.1 m, texels of 5 and 12 mm), and cascade 3 has a 39 m radius and 38 mm texels.
4. **Pipelines:**
   - `Pipeline::Config` gains `float depthBiasConstant = 0.0f` and `float depthBiasSlope = 0.0f`, in Vulkan's units; both 0 leave depth bias off. An empty `fragmentEntry` builds a vertex-only pipeline, and the blend state declares one attachment with a color format and none without.
   - `SceneRenderer` builds the shadow pipelines up front, next to the scene ones: `Shadow_Opaque` (vertex only) and `Shadow_Mask` (`fragShadowMasked`), with no color format, the depth format, `DepthMode::ReadWrite`, and a depth bias of constant 0 and slope −1.5 (see the pitfalls). They take the cull mode as dynamic state, like the scene pipelines. Blend materials don't cast shadows.
5. **Shadow pass:** `Renderer` opens one depth-only pass on the atlas, cleared to 0 (reverse-Z's far), and draws each cascade with `recordView` and its depth `SceneView`, the viewport and scissor set to its tile.
6. **Shading:**
   - `gpu::ShadowData` is uploaded per frame when a light casts shadows, and its address reaches `GlobalUniforms::shadows` through the camera's `SceneView`.
   - The light loop shadows only that light's direct term, which it finds by `GlobalUniforms::shadowLight`.
   - The cascade is the number of cascades that end before the pixel's view depth; four means past the shadow distance, which is lit.
   - The lookup moves along the geometry normal (the vertex normal, not the normal-mapped one, which varies per pixel) by 1.5 of the cascade's texels.
   - 3×3 PCF with `SampleCmpLevelZero` through the shadow sampler (`SamplerManager::kShadowSamplerIndex`). Each tap is clamped to its tile, half a texel in, so none reads a neighboring cascade. `SamplerComparisonState gShadowSamplers[]` is declared on binding 0 next to `gSamplers`: in SPIR-V both are plain samplers, and Vulkan allows two declarations of one binding.
7. **`DebugView::Cascades`:** the lit image tinted by cascade: red, green, blue, yellow, and untinted past the shadow distance. It's the lit image, so the tonemapper keeps PBR Neutral for it.
8. **`Renderer`:** `setShadowDistance` and `getShadowDistance`.
9. **`BasicApp`:** the sun casts shadows. The Lighting window gains the sun's Casts Shadows and the shadow distance, and the Debug View menu names Cascades. Alpha test pieces stand by the row:
   - a perforated panel under the center cube, facing the camera: a 1 m plane from `MeshUtils::createPlaneMeshData` (a rectangle facing +Z with the box's top-face UVs), pitched −90°, with a masked, double-sided material whose base color texture is made at startup, 8 × 8 round holes with soft one-texel edges;
   - a glass cube behind the left cube, inside its shadow: the unit cube at 0.6 m, blended, with alpha 0.35.

**Data:**
```cpp
// The cascades of the light that casts shadows, which color views shade with
struct ShadowData {
  Mat4 cascades[4];      // World → (atlas u, atlas v, depth) for each cascade
  Vec4 tileBounds[4];    // Each cascade's tile as (min u, min v, max u, max v), half a texel in from its edges
  Vec4 depthPlane;       // The camera's view depth of a world point p: dot(depthPlane.xyz, p) + depthPlane.w
  Vec4 splitDepths;      // The view depth where each cascade ends
  Vec4 normalOffsets;    // How far each cascade's lookups move along the normal, in meters
  uint32_t atlasTexture; // Packed like a material's textures: bindless index (low 16 bits), sampler index (high 16 bits)
  float texelSize;       // One atlas texel, in atlas UV
};
```
376 bytes. `GlobalUniforms` gains `VkDeviceAddress shadows` after `lights`, and `uint32_t shadowLight` after `debugView`: the light whose shadows were drawn, or `kNoShadowLight` (0xFFFFFFFF) when none were, and in depth views. That's 136 bytes, with no padding left. Offsets checked with slangc 2026.20 on Nov 1.

**Tests:**
- shadows (CPU), on BasicApp's camera and sun:
  - "Cascade Splits Rise To The Shadow Distance": at 3.411, 7.970, 18.703, and 60 m; at the far plane when that's nearer; none when the distance is inside the near plane;
  - "Every Slice Lands Inside Its Cascade": every corner of every slice lands at least 3.5 texels inside its tile (the border less snapping's half texel) and between its near and far planes, for a perspective and an orthographic camera, each in two poses;
  - "Cascades Keep World Points On Their Texels As The Camera Moves": moving the camera by fractions of a texel and by meters keeps a world point at the same spot within its texel in every cascade, and turning the camera keeps every cascade's texel size;
  - "A Cascade's Depth Falls Along The Light's Direction": a point farther along the light's +X gets a smaller depth (reverse-Z), and moving across the light keeps it.
- mesh, "A Plane Faces +Z With The Box's Top-Face UVs".
- renderer, "Frames Render Every Alpha Mode, Single- And Double-Sided": BasicApp's sun now casts shadows, so its cubes also draw through both shadow pipelines with both cull modes; its comment says so.
- renderer, "Frames Render Every Debug View": one frame in each, under validation.

**Done when:**
- The helmet and cubes cast shadows on the ground.
- There's no shadow acne and no detached shadows at the default bias.
- Shadows don't shimmer while the camera moves or rotates.
- The cascade debug view shows four bands.

**Pitfalls:**
- Reverse-Z flips the depth-bias signs: the bias must push depth toward 0, so the constant and slope factors are negative.
- A float depth format's constant bias is counted in steps of the depth value itself, and those shrink toward 0, which in reverse-Z is the far side of each cascade. It can't stand in for a distance in meters: the normal offset does that job, and the slope bias covers sloped surfaces.
- A sampled depth view must have only the depth aspect. The atlas has no stencil, so it avoids the problem.
- The `Shadow` sampler compares GREATER_OR_EQUAL. A fragment is lit when its light-space depth is at least the stored occluder depth (in reverse-Z, larger means closer to the light).
- Snap in the cascade's own texels (2048 to a tile), not the atlas's, and after building the whole view-projection.
- Don't compare a Slang pointer with null: Slang turns the address into a 64-bit integer, which needs `shaderInt64`. Mark what's absent with a count or an index instead, as `lightCount` and `shadowLight` do.
- Without depth clamp, a caster farther toward the light than casterMargin from a slice is clipped and casts nothing into it. 100 m covers scenes of BasicApp's size and indoor robot scenes; depth clamp, an optional feature, would lift the limit.
- The shadow pipelines keep `vertMain`, which also computes normals and tangents the depth pass doesn't use. That's small next to rasterizing four tiles; a lean vertex entry point can come with GPU profiling.
- Cascades depend on the camera, so every sensor view needs its own. Render Views decides whether to share or reduce them.
- Spot and point light shadows come after this phase: atlas tiles, six of them for a point light.

---

## Phase 6: Environment BRDF Table
**Status:** not started

**Goal:** The ambient's specular term reads a lookup table instead of Karis's `envBrdfApprox`, so rough metal reflects the sky at the right brightness. Only that term changes, and it changes before Image-Based Lighting, so a mismatch found in Phase 7 can't come from it.

The table answers one question: of light arriving evenly from every direction, what share does the specular lobe reflect toward the camera? The answer depends on the view angle (NdotV), the roughness, and F0, and comes out as a scale and an offset of F0: `F0 × A + B`. Karis fitted a curve to it in 2014, to save a texture read on the phones of the time. He fitted it to UE4's older Schlick-Smith visibility, not the height-correlated Smith of Phase 4's light loop. Against the exact values (the light loop's GGX and visibility, integrated on Nov 1):

| Roughness | NdotV | Metal (F0 = 1): exact | Fit | Plastic (F0 = 0.04): exact | Fit |
|---|---|---|---|---|---|
| 0.045 | 1.0 | 1.000 | 0.975 | 0.040 | 0.044 |
| 0.5 | 0.5 | 0.857 | 0.725 | 0.056 | 0.050 |
| 1.0 | 1.0 | 0.307 | 0.450 | 0.012 | 0.016 |
| 1.0 | 0.1 | 0.760 | 0.450 | 0.053 | 0.016 |

For a metal, the fit gives 1 − 0.55 × roughness at every angle, so mid-rough metal comes out about a quarter stop too dark, and rough metal loses its brightening toward the edges.

**Tasks:**
1. **Half floats:** a float → half conversion in `core/math` (moved here from Phase 7, which reuses it).
2. **Half-float textures:** `createTexture` takes only 8-bit RGBA today and builds an 8-bit mip chain. `ImageData` gains a half-float format, which `createTexture` uploads as it is, one level (`R16G16_SFLOAT` for two channels). Phase 7's HDR images reuse it.
3. **The table**, in `core` next to `ImageUtils` (CPU math, no Vulkan):
   - one function integrates one (NdotV, roughness) pair: GGX importance sampling over Hammersley points, with the light loop's height-correlated Smith visibility, returning A and B;
   - 64 × 64 texels, NdotV along x and perceptual roughness along y, each computed at its texel's center ((i + 0.5) / 64), with 256 samples.
4. **`ResourceManager`** builds the table once at startup and registers it, with the linear, clamp-to-edge sampler. A scratch version measured on Nov 1 took 24 ms in an optimized build and 45 ms in Debug.
5. **`GlobalUniforms`** gains `uint32_t envBrdfTexture`, the table's packed texture and sampler index (as in `gpu::Material`), after `shadowLight`: 140 bytes of fields, then 4 of padding, 144 in all (Phase 5 used the padding there was). `SceneRenderer` keeps the index from construction, like the material buffer's address.
6. **Shader:** `envBrdf(F0, roughness, NdotV)` reads the table and returns `F0 * AB.x + AB.y`, as `envBrdfApprox` did; `envBrdfApprox` goes away.
7. **Tests**, on the integration function:
   - roughness 1 seen head-on: A + B is 1 − ln 2 ≈ 0.307, within 0.005. At roughness 1, GGX spreads evenly and the integral has a closed form;
   - roughness 0.045 at NdotV 0.1, F0 0.04: within 0.005 of Schlick's Fresnel at that angle, 0.607. A near-mirror reflects what Fresnel says.

**Done when:**
- With the sun off in the Lighting window, so only the ambient lights the scene, MetalRoughSpheresNoTextures' rough metal spheres brighten toward their edges, and mid-rough metal is about a quarter stop brighter than with the fit.
- Smooth spheres barely change.

**Pitfalls:**
- Use the light loop's height-correlated Smith, not UE4's Schlick-Smith with k = α / 2, which most IBL tutorials use: that mismatch is part of what this phase fixes.
- Index the table by perceptual roughness, as `envBrdfApprox` was, not by α.
- Half floats, not 32-bit floats: linear filtering of `R32G32_SFLOAT` isn't guaranteed.
- The sampling error is largest at grazing angles (NdotV below 0.1): on Nov 1, at 256 samples, it was 0.4% on average and 2% at worst, there. More samples converge slowly; sampling visible normals (Heitz 2018) is the fix if it ever shows.

---

## Phase 7: Image-Based Lighting
**Status:** not started

**Goal:** An HDR environment map lights and reflects the scene and is drawn as the sky. The hemisphere stays as the fallback when there's no environment map.

**Tasks:**
1. **Image infrastructure:**
   - `Image::Config` gains `arrayLayers`, `VkImageCreateFlags flags` (`CUBE_COMPATIBLE`), and `viewType` (`CUBE`).
   - Extra 2D views, one per face and mip, to render into.
   - Mip generation with `vkCmdBlitImage`; linear blits of `R16G16B16A16_SFLOAT` are mandatory.
2. **HDR images:**
   - `ImageImporter::importHdrFromFile` loads Radiance `.hdr` files with `stbi_loadf` as float RGBA.
   - Convert them on the CPU to half floats (`R16G16B16A16_SFLOAT`), because linear filtering of `R32G32B32A32_SFLOAT` isn't guaranteed. They use Phase 6's float → half conversion and half-float `ImageData`.
3. **Assets:** a CC0 HDRI from Poly Haven (1k or 2k `.hdr`). **Decide first:** either build the planned download manifest (URL + SHA-256 into a gitignored cache, cached in CI) or commit one file of 2 MB or less.
4. **Precompute at load** (graphics passes, one fullscreen triangle per face and mip):
   - equirectangular map → 512² environment cube with full mips;
   - prefiltered specular cube, 256² with 6 mips; mip m holds roughness m / 5. Use GGX importance sampling that reads the environment's mips by sample density (filtered importance sampling, which avoids fireflies);
   - SH9 diffuse irradiance, computed on the CPU from the equirectangular pixels and weighted by solid angle.
5. **Bindless:** register cube views in binding 1 like 2D ones, and declare `TextureCube gCubeTextures[]` on binding 1 next to `gTextures`.
6. **`EnvironmentComponent`** gains the cube texture index, the SH coefficients, and `intensity` (nits per unit of the map).
7. **Shading:** when an environment map is set, it replaces the hemisphere:
   - diffuse ambient = SH(N) × diffuseColor;
   - specular = the prefiltered cube sampled at mip `roughness * 5` × the environment BRDF table (Phase 6);
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
  | NormalTangentTest, NormalTangentMirrorTest | normal maps, with tangents generated and from the file | 3 |
  | AlphaBlendModeTest | alpha modes | 3 |
  | MetalRoughSpheresNoTextures | the BRDF; the environment BRDF table | 4, 6 |

## Later (Outside This Plan)
- **Anti-aliasing:** MSAA on the scene target suits forward rendering; decide before Render Views. An entity-ID target can't be averaged, so resolve it with sample zero.
- **Entity-ID attachment** for segmentation sensors and picking.
- **Runtime material edits** (domain randomization): add a barrier before overwriting a slot, or upload materials per frame.
- **Spot and point light shadows.**
- **More glTF features:** TEXCOORD_1, KHR_texture_transform, embedded (`data:`) images, KHR_lights_punctual, the KHR_materials_* extensions. A KHR_lights_punctual light shines along glTF −Z, which the axis change makes engine −X, so the importer turns it half a turn about its up axis to shine along +X.
- **Auto exposure:** metering the scene as real cameras do, for camera sensors.
- **Shading quality:** specular anti-aliasing and multiple-scattering energy compensation (which reads Phase 6's table).
- **Clustered lighting**, if light counts grow.
- **Render graph:** see Decisions for when to revisit it.
