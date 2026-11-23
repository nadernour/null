# Rendering Plan

Physically based rendering for Null, split into seven phases. Each phase leaves the engine working and can be checked on its own. The README's Rendering section lists the same phases.

Phases 1–5 are done. Their task lists are in git history (`git show 32ada07:rendering_plan.md`), and what they learned that still applies is under **Pitfalls From Done Phases**.

## How to Work Through It

- One phase per session, in order. Commit each phase on its own.
- Before starting, read **Decisions**, **Pitfalls From Done Phases**, and the phase.
- Every phase ends with CLAUDE.md's **Verifying** checks and these, on top of its own **Done when** list:
  1. An interactive run (`./build/debug/bin/null_engine`) survives a few window resizes and exits cleanly.
  2. Before / after screenshots of BasicApp match the phase's expected look.
  3. The phase's README item is ticked and its **Status** below says done.

## Decisions

### Frame and render targets
- **Ownership:** `Application` holds the `Window`, the `Renderer`, and the `Registry`, and runs the loop and the UI hooks. `Renderer` owns everything on the GPU side: the instance, the device, `FrameManager`, `ResourceManager`, `ImGuiManager`, the classes that draw, and the render targets. *Why:* the app holds rendering API objects, not their implementation, so it never has to keep two of them in agreement (such as the formats `SceneRenderer`'s pipelines and the targets share), and apps built on the core library don't repeat the setup.
- **No render graph.** `Renderer` is the only place that orders passes; a pass is one `vkCmdBeginRendering` scope, as in Vulkan. It records every barrier, opens every pass (attachments, load ops, clear values, viewport), and hands the classes that draw into a pass their per-frame resources (the command buffer, the frame's upload allocator); those classes only draw. `Image` builds its own barriers from the layout, access, and stage it already tracks, and back-to-back barriers go out together in one `vk_utils::pipelineBarrier`. *Why:* the frame is a fixed sequence, and later a loop over views. One pass can hold several draws (the tonemap and the UI; later the shadow cascades in one atlas), so a pass can't belong to what draws into it. Revisit only if the frame becomes data-driven (user-defined sensor pipelines), many short-lived post passes pile up, or profiling calls for async compute.
- **No command encoder.** The classes that draw record into the command buffer themselves, with two helpers: `gpu::pushConstants`, which always writes the shared range with its stages, and `Renderer`'s `setViewportAndScissor`, which takes a rectangle so each shadow cascade gets its tile. *Why:* multi-draw indirect keeps a view to a handful of binds, so an encoder that skips repeated binds saves nothing, and its record of what's bound goes stale whenever ImGui's backend binds its own pipeline, index buffer, and set 0. Nothing on the roadmap needs a second graphics API to hide Vulkan behind. Revisit if recording moves onto several threads (Parallel Environments) or compute and utility passes start repeating the same bind, push, and draw steps. GPU Profiling's timestamps and RenderDoc labels go where `Renderer` opens and closes each pass.
- **Per-frame uploads:** each frame in flight has an `UploadAllocator` in `FrameManager`: 16 MB host-visible blocks filled in order, the next block taking over when one is full (an upload larger than a block gets a block of its own size), all reused once that frame's fence has signaled. *Why:* nothing predicts a frame's upload size, so adding a per-frame upload is one call. A prediction that came out short would overflow the buffer in Shipping, where asserts are compiled out.
- **Scene color format:** `VK_FORMAT_R16G16B16A16_SFLOAT`. *Why:* color attachment, blending, and sampling are mandatory for it.
- **Depth format:** `VK_FORMAT_D32_SFLOAT` from `Device::findDepthFormat()`, which falls back only to other depth-only formats; looked up once in `Renderer`. *Why:* nothing uses stencil, and a depth-only image can be sampled through its one view (the Hi-Z pyramid later).
- **Swapchain format:** prefer `VK_FORMAT_B8G8R8A8_UNORM`, and let the tonemap shader apply the sRGB encode. A push-constant flag skips the encode if the surface only offers an `_SRGB` format. *Why:* ImGui's colors are sRGB values meant to be written as-is. On an `_SRGB` swapchain they get encoded twice and look washed out.
- **Sampled render targets:** one bindless slot each, kept across resizes by `BindlessManager::updateSampledImage`. Rewrite a slot only while the device is idle. *Why:* fits the all-bindless design, with no new descriptor mechanism and no extension. Push descriptors stay on the roadmap for compute passes that write storage images.
- **Pipeline layout:** one layout for every graphics pipeline. Set 0 is the bindless set, plus one 128-byte push-constant range for `VERTEX | FRAGMENT`. *Why:* fragment shaders need push constants too, and with one layout set 0 stays bound across passes.
- **Names:** `Renderer` for the frame orchestrator. Classes that draw into a pass are named for what they draw (`SceneRenderer`, `Tonemapper`). `FrameManager` owns the swapchain and the frames in flight.

### Shading and units
- **Tonemapper:** Khronos PBR Neutral, with no exposure inside it. *Why:* it keeps base colors faithful, which camera sensors want.
- **Exposure:** computed from the primary camera's aperture, shutter speed, and ISO (EV100). It's applied as pre-exposure in the scene shader. *Why:* real cameras expose this way, and pre-exposure keeps fp16 from overflowing.
- **Units:**
  - directional lights: lux;
  - point and spot lights: candela;
  - emissive and sky: nits (cd/m²);
  - emissive from file formats that give it no unit (glTF now; OBJ, DAE, and MJCF later): multiplied by `kUnitlessEmissiveNits` (1000).

  *Why:* these are KHR_lights_punctual's units. Emissive in those formats has no unit, so the constant is the one place to tune it. It's one value for all of them, so the Emissive debug view, which divides it back out, shows every format's emissive as authored. Godot's materials default to the same 1000 nits.
- **Light direction:** a light shines along its entity's +X (forward), like cameras.
- **Ambient until IBL:** a sky / ground hemisphere with both a diffuse and a specular term (scaled by `envBrdfApprox` until Phase 6's table replaces it). *Why:* without a specular ambient term, metals render black when only punctual lights hit them.
- **Debug views:** a channel view shows what the Khronos Sample Viewer's debug channel shows: data (normals, tangents, metallic, roughness, occlusion, UVs) as stored, base color and emissive as colors, and no PBR Neutral. The Emissive view divides emissive by `kUnitlessEmissiveNits`, so it shows the authored value. *Why:* the Sample Viewer is the reference renderer, so each view can be checked against it.

### Materials and meshes
- **Material storage:** a device-local buffer of `gpu::Material`, append-only, written at load. The per-instance `colorTint` stays for runtime variation. *Why:* overwriting a slot that an in-flight frame reads needs a barrier, and nothing needs runtime edits before Episodes & Resets.
- **Missing textures:** use the white fallback (index 0). A material flag switches normal mapping on. *Why:* white is neutral for every slot except normals, and it avoids a branch per texture.
- **Samplers:** one immutable sampler per texture filter (linear, nearest) × U wrap × V wrap (repeat, clamp, mirror), 18 in all, on binding 0; a texture's settings index them directly. The shadow sampler, which compares depths, has binding 2 to itself. *Why:* glTF wraps each axis separately, and TextureSettingsTest mixes wrap modes on every test tile. Shaders declare a comparison sampler as a type of its own (`SamplerComparisonState`): on binding 0 it took a second declaration of that binding, which Slang warns about (E39001), and an index packed next to the atlas's. The bindless set layout is update-after-bind, so its samplers count only against the update-after-bind limits, not `maxPerStageDescriptorSamplers` (whose required minimum is 16).
- **Tangents:**
  - use glTF `TANGENT` when present;
  - otherwise the glTF importer generates them with MikkTSpace (vendored `mikktspace.c/.h`), negates `w`, and welds the vertices again, but only for a primitive whose material kept a normal texture;
  - the shader applies them as `mikktspace.h` prescribes: the bitangent is `w * cross(N, T)` per pixel, from the interpolated normal and tangent as they arrive, and only the final normal is normalized.

  *Why:* glTF names MikkTSpace for missing tangents, and MikkTSpace bakers write normal maps that invert exactly this transform. glTF normal maps put +Y toward the top of the image, while UV v points down. glTF calculates tangents only for a material with a normal texture, from that texture's UVs, and only the importer sees a primitive's material.
- **Missing normals:** the glTF importer generates flat normals (one vertex per index), welds back the corners that triangles on one flat face share, and drops the file's tangents. *Why:* glTF requires both, and the weld keeps flat faces from costing three vertices per triangle.
- **Alpha and double-sided:** one pipeline per alpha mode {opaque, mask, blend}, all built up front by `SceneRenderer`. The cull mode is dynamic state (core since Vulkan 1.3), set per batch: back faces for single-sided materials, none for double-sided ones. Draws sort by alpha mode: opaque, mask, then blend, back to front; then single-sided before double-sided. Blend draws that share a submesh and end up next to each other still merge into one instanced draw. *Why:* opaque draws keep early depth testing, and drawing them first fills the depth buffer before the masked draws, whose `discard` keeps them from writing depth early. Blending happens in the right order: Vulkan draws instances in order, so merged blend draws stay back to front. With the cull mode dynamic, sidedness doesn't double the pipelines, and building them up front means none compiles mid-run.
### Later phases
- **Shadows:** directional cascaded shadow maps (CSM), 4 cascades in one 4096² D32 atlas, stabilized, drawn with depth clamp. *Why:* an atlas needs no array images, and per-view drawing is shared with Render Views. Depth clamp flattens casters in front of a cascade's near plane onto it instead of clipping them, so casters however far toward the light still cast; `Device` requires the `depthClamp` feature, which desktop drivers and lavapipe have.- **Environment BRDF:** a 64 × 64 `R16G16_SFLOAT` table, built on the CPU at startup from the light loop's GGX and visibility, replaces `envBrdfApprox`, in its own phase before IBL. With it, the ambient adds back the light rough surfaces bounce between their microfacets (Fdez-Agüera's multiple scattering, as the Sample Viewer's image-based light does) and gives its diffuse only what the specular leaves. The light loop keeps one bounce, as the Sample Viewer's does. *Why:* the fit makes mid-rough metal about a quarter stop too dark and drops rough metal's brightening toward the edges (Phase 6 has the numbers), which a comparison with the Sample Viewer, which reads a table, would show. One bounce alone leaves white rough metal up to 1.7 stops darker than the reference, and the unweighted diffuse makes surfaces seen at a grazing angle send back more light than they receive. In its own phase it stays out of IBL's way.- **IBL:**
  - `.hdr` loaded with stb_image into an RGBA16F cube with prefiltered mips, built with graphics passes;
  - the map's middle faces +X and its top +Z, as in Blender;
  - SH9 diffuse computed on the CPU, stored as irradiance / π like `hemisphere(N)`;
  - Phase 6's ambient (table, multiple scattering, diffuse weight), with the map in place of the hemisphere;
  - cube views share binding 1 with the 2D textures;
  - the sky draws inside the scene pass, after the masked draws and before the blended ones.

  *Why:* graphics passes reuse color-attachment rendering, so no storage images or compute pipelines are needed yet. Blender's orientation makes a map face the same way in both, and its world is Z-up too. Cube and 2D views are both `SAMPLED_IMAGE` descriptors, so one binding keeps one slot allocator, at the price of Slang's overlap warning (39001), off for that one declaration. The shadow sampler has a binding of its own because it's one fixed sampler that needs no slot; cube maps are an open-ended set, which a binding of their own would need a second allocator and a fixed size for.
---

## Pitfalls From Done Phases
What Phases 1–5 learned that still applies to new code.

**Shaders and layouts**
- Slang reads buffer pointers with natural (scalar) layout, so C++ structs match as-is. Push constants use std430: keep push-constant structs to 64-bit addresses and 32-bit scalars, never `float3`. To check a layout, run `build/debug/_deps/slang-src/bin/slangc <file> -target spirv-asm ...` and read the `OpMemberDecorate ... Offset` lines.
- Don't compare a Slang pointer with null: Slang turns the address into a 64-bit integer, which needs `shaderInt64`, a feature the engine doesn't enable. Mark what's absent with a count or an index instead, as `lightCount` and `shadowLight` do.
- Index descriptor arrays with `NonUniformResourceIndex`. One instanced draw holds instances with different materials and samplers, and indexing without it is undefined behavior that shows up only on some GPUs.
- `CMakeLists.txt` compiles every `shaders/*.slang` file, and each command depends only on its own file. Until a shared module is really needed, duplicate the bindless declarations. If you add one, exclude it from the compile loop and add it to every shader's `DEPENDS`.
- `gpu::DebugView` and the Slang enum list the views in the same order, and nothing checks it. `-Wswitch` flags a view that MainUI doesn't name.

**Pipelines and passes**
- `Pipeline` culls back faces with counter-clockwise front faces. The usual fullscreen-triangle formula `uv = float2((id << 1) & 2, id & 2)` gives a clockwise triangle in Vulkan, which gets culled, so fullscreen passes (the tonemapper) use `VK_CULL_MODE_NONE`.
- Bindless descriptors are written with layout `SHADER_READ_ONLY_OPTIMAL`: transition a render target to it before a pass samples it.
- Both frames in flight share the scene's color and depth images. That's fine: the tracked state makes each frame's first barrier wait for the previous frame's last use.
- Put `discard` only in the masked entry points, so opaque draws keep early depth testing. Blended pixels test against the opaque depth but don't write it.
- A sampled depth view must have only the depth aspect.

**Materials and tangents**
- Color space is decided per use, not per file. An `_SRGB` normal or metallic-roughness texture breaks shading silently.
- glTF metallic-roughness: G is roughness and B is metallic. Occlusion is R, often in the same image (an ORM texture). glTF's default material is fully metallic; the engine's own default is a dielectric.
- Negate MikkTSpace's `w`, or equivalently run it on `1 − v`, or the bumps look inverted: easy to miss on the helmet, obvious on NormalTangentTest, and the box test catches it first. Its output is one vertex per index, so weld again, or vertex counts triple.
- Compute B before the back-face flip. `cross(-N, -T)` equals `cross(N, T)`, so a B computed after the flip doesn't flip, and back faces get a mirrored bump instead of the reverse one.
- Known gaps: mirrored instances (negative scale determinant) don't flip the tangent handedness (the importer warns about mirrored nodes), and a double-sided blend mesh draws its back and front faces in index order, so a closed transparent mesh can blend in the wrong order (the fix is a back-faces-first pass).

**Lighting and exposure**
- Exposure is applied once, as pre-exposure in the scene shader; the tonemap must not apply it again. Pre-exposure keeps fp16 from overflowing, and `min(..., 64000)` catches the rest: at BasicApp's exposure, a mirror-like highlight under the 1000 lux sun reaches about 67,000, past fp16's maximum of 65,504.
- A light shines along +X, so a directional light's `position` is `-direction`, the direction toward the light.
- Skip a light with NdotL ≤ 0 before computing H. `normalize(V + L)` is NaN when L = −V (a light shining straight into the camera), and a NaN times a zero NdotL is still NaN.
- An orthographic camera's eye is `(-forward, 0)`, not its position: its rays are parallel, and using the position gives flat surfaces a highlight that follows the camera.
- Physical units span a huge range. A 100,000 lux sun at f/16, 1/125 s, ISO 100 looks normal, and so does 1000 lux at f/4, 1/60 s, ISO 400. Pairing one setup's lights with the other's exposure looks all black or all white; that's correct, not a bug. The defaults pair a sunny-16 camera with KHR_lights_punctual's intensity of 1, so a new light needs its intensity set.

**Shadows**
- Reverse-Z flips the depth-bias signs: the bias must push depth toward 0, so the constant and slope factors are negative.
- A float depth format's constant bias is counted in steps of the depth value itself, and those shrink toward 0, which in reverse-Z is the far side of each cascade. It can't stand in for a distance in meters: the normal offset does that job, and the slope bias covers sloped surfaces.
- `kNormalOffsetTexels` (1.5) and `kBorderTexels` (4) follow from the 3 × 3 filter, the slope bias, and the snapping: change any of those and redo both. Each of the filter's nine taps compares bilinearly against four texels, so a lookup reads texels up to two away. On a surface tilted by an angle a from the light, the depth stored there is up to 2 tan(a) texels closer to the light than the surface. The slope bias (1.5 times the depth's slope across a texel) clears 1.5 tan(a) of that, and moving the lookup d texels along the normal clears d / cos(a), so d needs only 0.5 sin(a): 1.5 covers every angle with room to spare and gives a surface facing the light 1.5 texels of clearance. The border holds what reaches past a slice's bounding sphere: half a texel of snapping, the normal offset's 1.5, and the filter's two. Each light view's near plane lies on its tile's box, so the border also keeps every lookup behind that plane.
- The `Shadow` sampler compares GREATER_OR_EQUAL. A fragment is lit when its light-space depth is at least the stored occluder depth (in reverse-Z, larger means closer to the light).
- Snap in the cascade's own texels (2048 to a tile), not the atlas's, and after building the whole view-projection.
- The shadow pipelines keep `vertMain`, which also computes normals and tangents the depth pass doesn't use. That's small next to rasterizing four tiles; a lean vertex entry point can come with GPU profiling.

---

## Phase 6: Environment BRDF Table
**Status:** not started

**Goal:** The ambient reads a lookup table instead of Karis's `envBrdfApprox`, and with it:
- the specular term reflects the sky at the right brightness at every roughness;
- the light that rough surfaces bounce between their microfacets, which a one-bounce BRDF loses, comes back (multiple scattering);
- the diffuse term gets only the light the specular doesn't reflect.

The light loop doesn't change. All of it lands before Image-Based Lighting, so a mismatch found in Phase 7 can't come from it.

The table answers one question: of light arriving evenly from every direction, what share does the specular lobe reflect toward the camera? The answer depends on the view angle (NdotV), the roughness, and F0, and comes out as a scale and an offset of F0: `F0 × A + B`. Karis fitted a curve to it in 2014, to save a texture read on the phones of the time. He fitted it to UE4's older Schlick-Smith visibility, not the light loop's height-correlated Smith. Against the exact values (the light loop's GGX and visibility, integrated on Nov 1):

| Roughness | NdotV | Metal (F0 = 1): exact | Fit | Plastic (F0 = 0.04): exact | Fit |
|---|---|---|---|---|---|
| 0.045 | 1.0 | 1.000 | 0.975 | 0.040 | 0.044 |
| 0.5 | 0.5 | 0.857 | 0.725 | 0.056 | 0.050 |
| 1.0 | 1.0 | 0.307 | 0.450 | 0.012 | 0.016 |
| 1.0 | 0.1 | 0.760 | 0.450 | 0.053 | 0.016 |

For a metal, the fit gives 1 − 0.55 × roughness at every angle, so mid-rough metal comes out about a quarter stop too dark, and rough metal loses its brightening toward the edges.

**Multiple scattering.** A rough surface is a field of tiny mirror facets, tilted every which way. The BRDF follows light through one bounce: light that one facet sends into a neighbor counts as lost, though in a real metal most of it bounces on and leaves. The loss grows with roughness. The table above says a perfect metal (F0 = 1) at roughness 1, seen head-on, reflects 0.307 of an even sky, so 69% goes missing. Fdez-Agüera ("A Multiple-Scattering Microfacet Model for Real-Time Image-Based Lighting", 2019) adds it back from the same table. A + B is the share a perfect metal keeps after one bounce, so 1 − (A + B) is what goes on bouncing, and each later bounce keeps F_avg of it: Schlick's Fresnel averaged over the hemisphere, F0 + (1 − F0) / 21.
- The perfect metal: F_avg = 1, and the bounced light adds 0.693 × 0.307 / (1 − 0.693) = 0.693, so it reflects 0.307 + 0.693 = 1, all of the light, at every roughness and angle.
- Plastic barely moves: at the same roughness and angle, F0 = 0.04 goes from 0.012 to 0.013, since each later bounce keeps less than a tenth of its light.
- Colored metals come out a little more saturated, as real ones do: every bounce tints the light again.

**Diffuse weight.** Of the light reaching a surface, the specular reflects a share and the rest enters the material, which the diffuse term sends back out. Today's ambient adds both in full. Smooth white plastic seen at a grazing angle (NdotV = 0.1, the way a low robot camera sees the floor) reflects 0.607 of the sky, and the diffuse adds 1.0 more: 1.6 times the light it receives. Weighted, it's (1 − 0.607) × 1.0 + 0.607 = 1.0. Head-on the error was 4%, since only 0.04 reflects there.

**Tasks:**
1. **Half floats:** a float → half conversion in `core/math` (moved here from Phase 7, which reuses it).
2. **Half-float textures:** `createTexture` takes only 8-bit RGBA today and builds an 8-bit mip chain. `ImageData` gains a half-float format, which `createTexture` uploads as it is, one level (`R16G16_SFLOAT` for two channels). Phase 7's HDR images reuse it.
3. **The table**, in `core` next to `ImageUtils` (CPU math, no Vulkan):
   - one function integrates one (NdotV, roughness) pair: GGX importance sampling over Hammersley points, with the light loop's height-correlated Smith visibility, returning A and B;
   - 64 × 64 texels, NdotV along x and perceptual roughness along y, each computed at its texel's center ((i + 0.5) / 64), with 256 samples.
4. **`ResourceManager`** builds the table once at startup and registers it, with the linear, clamp-to-edge sampler. A scratch version measured on Nov 1 took 24 ms in an optimized build and 45 ms in Debug.
5. **`GlobalUniforms`** gains `uint32_t envBrdfTexture`, the table's packed texture and sampler index (as in `gpu::Material`), after `shadowLight`: 140 bytes of fields, then 4 of padding, 144 in all. `SceneRenderer` keeps the index from construction, like the material buffer's address.
6. **Shader:** `envBrdf(F0, roughness, NdotV)` reads the table and returns the share of an even sky the specular reflects, multiple scattering included; `envBrdfApprox` goes away.
   ```slang
   float2 AB = sampleTexture(pc.globalUniforms->envBrdfTexture, float2(NdotV, roughness)).rg;
   float3 FssEss = F0 * AB.x + AB.y;     // One bounce
   float Ems = 1.0 - (AB.x + AB.y);      // What a perfect metal loses past one bounce
   float3 Favg = F0 + (1.0 - F0) / 21.0; // Schlick's Fresnel averaged over the hemisphere
   return FssEss + Ems * FssEss * Favg / (1.0 - Favg * Ems);
   ```
   The ambient gives the diffuse what the specular leaves:
   ```slang
   float3 specularShare = envBrdf(F0, roughness, NdotV);
   radiance += (diffuseColor * (1.0 - specularShare) * hemisphere(N) + specularShare * hemisphere(R)) * ao;
   ```
   Land it in two steps and screenshot each: first `F0 * AB.x + AB.y` in place of the fit, with the ambient as it is, then the multiple scattering and the diffuse weight. Each step then shows on its own (see **Done when**).
7. **Tests**, on the integration function:
   - roughness 1 seen head-on: A + B is 1 − ln 2 ≈ 0.307, within 0.005. At roughness 1, GGX spreads evenly and the integral has a closed form;
   - roughness 0.045 at NdotV 0.1, F0 0.04: within 0.005 of Schlick's Fresnel at that angle, 0.607. A near-mirror reflects what Fresnel says.

**Done when**, with the sun off in the Lighting window, so only the ambient lights the scene:
- After the first step (the table alone), MetalRoughSpheresNoTextures' rough metal spheres brighten toward their edges, mid-rough metal is about a quarter stop brighter than with the fit, and smooth spheres barely change.
- After the second, rough metal brightens again, a white metal by up to 1.7 stops (0.307 → 1 at roughness 1, seen head-on), while the non-metal spheres barely change. The far ground, seen at a grazing angle, darkens slightly.
- **White furnace:** with the sky and the ground set to one color and intensity, a white cube (in a local change to BasicApp), metal or not and at any roughness, shows all its faces at one brightness, so its edges disappear: a surface that absorbs nothing reflects all of an even sky, whichever way it faces. Before this phase, rough metal looks darker, and plastic faces seen at a grazing angle look brighter.

**Pitfalls:**
- Use the light loop's height-correlated Smith, not UE4's Schlick-Smith with k = α / 2, which most IBL tutorials use: that mismatch is part of what this phase fixes.
- Index the table by perceptual roughness, as `envBrdfApprox` was, not by α.
- Half floats, not 32-bit floats: linear filtering of `R32G32_SFLOAT` isn't guaranteed.
- The sampling error is largest at grazing angles (NdotV below 0.1): on Nov 1, at 256 samples, it was 0.4% on average and 2% at worst, there. More samples converge slowly; sampling visible normals (Heitz 2018) is the fix if it ever shows.
- Don't copy the Sample Viewer's "roughness-dependent Fresnel" (`k_S` in `getIBLGGXFresnel`, from the same paper), which replaces F0 with a Fresnel taken at NdotV before the table. The table's A and B already integrate Fresnel over the lobe, so it counts Fresnel twice: smooth plastic at NdotV = 0.1 reflects 0.83 instead of Fresnel's 0.607. Smooth non-metals at grazing angles look a little dimmer here than in the Sample Viewer, and that difference is expected.
- The light loop keeps one bounce, as the Sample Viewer's does, so rough metal lit by the sun stays darker than the same metal lit by the sky. Adding it there is in **Later**.

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
   - **Orientation:** the image's middle column faces +X (forward), the column a quarter of the way in from the left +Y (left), and the top row +Z: standing at the origin and looking forward, you see the middle of the panorama the right way round. For a direction d, u = 0.5 − atan2(d.y, d.x) / 2π and v = acos(d.z) / π, with v = 0 the top row. That's Blender's horizontal mapping (Cycles' `direction_to_equirectangular`), whose world is Z-up too, so a map faces the same way in both. USD dome lights put their pole on +Y (`UsdLuxDomeLight`), so a dome light from a USD scene needs a rotation to match.
3. **Assets:** a CC0 HDRI from Poly Haven (1k or 2k `.hdr`). **Decide first:** either build the planned download manifest (URL + SHA-256 into a gitignored cache, cached in CI) or commit one file of 2 MB or less.
4. **Precompute at load** (graphics passes, one fullscreen triangle per face and mip):
   - equirectangular map → 512² environment cube with full mips;
   - prefiltered specular cube, 256² with 6 mips; mip m holds roughness m / 5. Use GGX importance sampling that reads the environment's mips by sample density (filtered importance sampling, which avoids fireflies);
   - SH9 diffuse, computed on the CPU from the equirectangular pixels weighted by solid angle, and stored as irradiance / π: what `hemisphere(N)` returns today, so a map of one radiance L gives L at every normal.
5. **Bindless:** register cube views in binding 1 like 2D ones (both are `SAMPLED_IMAGE` descriptors, so `BindlessManager` keeps one slot allocator), and declare `TextureCube gCubeTextures[]` on binding 1 next to `gTextures`. Slang warns about the overlap (39001), so the warning is off for that one declaration, between `#pragma warning(push)` and `#pragma warning(pop)`. Reading a slot through the wrong declaration (a cube through `gTextures`) is undefined.
6. **`EnvironmentComponent`** gains the cube texture index, the SH coefficients, and `intensity` (nits per unit of the map). They reach the shader the way the shadows do: `SceneRenderer` uploads a `gpu::Environment` (the SH and the intensity) once per frame, and `GlobalUniforms` gains its address and `uint32_t environmentMap`, the cube's packed index, or `kNoEnvironmentMap` (`~0U`) for the hemisphere. A pointer can't be compared with null (see **Pitfalls From Done Phases**).
7. **Shading:** when an environment map is set, it replaces the hemisphere in Phase 6's ambient, which stays as it is otherwise:
   - diffuse = SH(N) × diffuseColor × (1 − specularShare);
   - specular = the prefiltered cube sampled along R at mip `roughness * 5`, × specularShare;
   - both multiplied by intensity and ao.
8. **Sky:** `SceneRenderer::recordView` draws it in color views when the environment has a map, after the last masked batch and before the first blended one (batches are sorted by alpha mode), through a `Sky` pipeline built up front with the others:
   - `vertSky` and `fragSky` in `scene.slang`, so it shares the bindless declarations and `GlobalUniforms`;
   - a fullscreen triangle at depth 0 (reverse-Z's far), with no vertex input and cull mode none (see **Pitfalls From Done Phases**);
   - `DepthMode::ReadOnly`, which tests GREATER_OR_EQUAL and writes no depth, so it shades only the pixels nothing covers;
   - the direction from the inverse view-projection (`GlobalUniforms` gains `Mat4 invViewProj`): the pixel's ray through two depths, so an orthographic camera sees one direction everywhere, as its eye does;
   - color = environment cube × intensity × exposure.

   *Why there:* opaque and masked draws fill depth first, so the sky shades only what's left. Blended draws don't write depth, so a sky drawn after them would pass the depth test and paint over glass seen against the sky.

   `GlobalUniforms` grows by `invViewProj`, the environment's address, and `environmentMap`; check the offsets with slangc (see **Pitfalls From Done Phases**).

**Done when:**
- The helmet's metal reflects the environment.
- Rough surfaces show a blurred reflection.
- Diffuse lighting takes on the environment's color.
- The sky shows behind the scene, without seams at cube edges.
- Glass seen against the sky still shows over it.
- **Furnace with a sky:** with an environment map of one color, Phase 6's white cube vanishes into the sky, which shows that the sky and the lighting share units and exposure.

**Tests:**
- image, "Equirectangular Pixels Map To REP-103 Directions": the middle of the middle row faces +X, a quarter of the way in from the left +Y, three quarters −Y, the top row +Z, and the bottom row −Z; direction → pixel → direction comes back.
- IBL (CPU):
  - "A Constant Environment's SH Gives It Back At Every Normal": a 64 × 32 map of one radiance L evaluates to L within 0.5% at the six axis normals and four diagonals.
  - "A Two-Color Sky's SH Matches The Hemisphere": the sky's radiance above the horizon and the ground's below evaluate to `hemisphere(N)` within 1%. In words: a two-color sky's diffuse light changes only linearly with N.z, which the first two SH bands hold exactly, and a step at the horizon has nothing in the third band, so nine coefficients reproduce it without ringing; a bright sun is what rings (see the pitfalls). It also means that swapping the hemisphere for the same sky as a map leaves the diffuse unchanged.
- renderer, "Frames Render With An Environment Map": an environment map made from a generated 64 × 32 two-color image, so CI needs no asset. `runForFrames(3)` under validation covers the precompute passes, the cube views, and the sky; then BasicApp's environment goes back.

**Pitfalls:**
- An HDRI with a visible sun lights the scene twice when a directional light also stands in for the sun. Pick sunless HDRIs, or keep the sun out of the map.
- Nine SH coefficients can't hold a small, bright source. Around a sun in the map they ring: dark or tinted blotches on the side facing away from it, even negative light. Sunless maps avoid it; **Later** has the fixes.
- The Sample Viewer lights its diffuse from a filtered cube, not SH9. For sunless maps the two agree to about 1% on average (Ramamoorthi & Hanrahan, 2001), so compare the diffuse loosely and the specular closely.
- HDRIs come in arbitrary units. Calibrate `intensity` against the directional light; a sunny sky is a few thousand nits.
- Equirectangular latitude is measured from +Z (engine up), not +Y as most sample code assumes, and longitude from +X (task 2).

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
- **Shadows per sensor** (Render Views): cascades follow their camera, so each sensor needs its own, out to its own `CameraComponent::shadowDistance`. A 1 cm near plane and a 2 m distance give 1.2 screen pixels per texel at 20 cm (BasicApp's 45° camera, 1000 px tall); BasicApp's 60 m distance gives 12.9 there. For bounded scenes, one map fitted to the scene and shared by every sensor costs one shadow pass in all instead of four per sensor: a 4096² map over a 4 m scene gives 5.9 pixels per texel at 20 cm, fine for sensors that stay a meter or more from what they see.- **Ray-traced shadows** (README, Sensors), with CSM as the fallback, chosen per run. When they or spot and point light shadows come, each `gpu::Light` gets a shadow index (`kNoShadowLight` for none) in place of `GlobalUniforms::shadowLight`, so the light loop keeps asking `shadowFactor` whatever answers it.
- **Culling shadow views** (README, Compute Frustum & Occlusion Culling): a cascade's view must not cull against its near plane. Depth clamp is what lets casters however far toward the light still cast, and a near-plane test would drop them again. Cull against the four sides and the far plane only.- **Shadow filtering:** Castaño's tent weights for the PCF taps (a smoother 5 × 5 tent from the same 9 taps, or today's softness from 4), and neighboring cascades blended over a band at each split, so the edge's softness doesn't jump where cascades meet (texels grow from 2.3 to 5 mm at BasicApp's 3.4 m split).
- **More glTF features:** TEXCOORD_1, KHR_texture_transform, embedded (`data:`) images, KHR_lights_punctual, the KHR_materials_* extensions. A KHR_lights_punctual light shines along glTF −Z, which the axis change makes engine −X, so the importer turns it half a turn about its up axis to shine along +X.
- **Auto exposure:** metering the scene as real cameras do, for camera sensors.
- **Shading quality:**
  - specular anti-aliasing;
  - multiple scattering in the light loop. Phase 6 adds it to the ambient only, which leaves rough metal darker under the sun than under the sky. Filament adds it to punctual lights as one factor on the specular lobe, 1 + F0 × (1 / (A + B) − 1), read from Phase 6's table; the Sample Viewer doesn't, so adding it moves away from the reference. A comparison with real camera images should decide.
- **Ambient occlusion:** once the sky lights the scene (Phase 7), nothing blocks it but a material's occlusion texture, so the floor under a robot or a table gets the full sky and objects look like they float. Screen-space GTAO is the usual raster answer; ray-traced occlusion can come with Ray-Traced Shadows, on Ray-Query LiDAR's acceleration structures. Either darkens only the ambient: the punctual lights' occlusion is their shadows.- **Suns in HDRIs:** Episodes & Resets randomizes lighting, and swapping HDRIs is the usual way, but most outdoor HDRIs hold a sun, which makes SH9 ring and lights the scene twice next to the directional light (Phase 7's pitfalls). The usual fixes, often used together: window the SH coefficients (Sloan, "Stupid Spherical Harmonics (SH) Tricks", 2008), and pull the sun out of the map into the directional light: find its brightest disc, set the light's direction and lux from it, and clamp it out of the map, so the sun also casts the cascaded shadows.- **Clustered lighting**, if light counts grow.
- **Render graph:** see Decisions for when to revisit it.
