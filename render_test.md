# Render Test Assets

**Status:** planned, not started

**Goal:** The Khronos glTF test models can be picked from a window in the running app. They're downloaded at configure time from one zip on this repo's GitHub releases. Today each check is a local change to `BasicApp` that loads one model by hand (`rendering_plan.md`, Verification Assets), so a model gets checked once, in the phase that needed it, and rarely again.

## Decisions
- **Source:** the models come from `KhronosGroup/glTF-Sample-Assets` at a pinned commit, `e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea` (Nov 20, 2026). *Why:* a pinned commit is stable, and Khronos maintains these models as glTF's conformance and rendering tests.
- **Hosting:** one zip on a release of this repo, tagged `test-assets-v1`. *Why:* the Khronos repo is about 1.8 GB, too big to clone in a build, and taking only some models from it means downloading every `.gltf`, `.bin`, and texture one by one. A zip is one download with one SHA-256, and it keeps working if Khronos reorganizes the repo, as it did in 2023 when it replaced glTF-Sample-Models. The cost is a script to rebuild the zip when the list changes.
- **Licenses:** every model folder in the zip keeps its `LICENSE.md` and `metadata.json`. *Why:* the licenses vary by model: many are CC0 (public domain), and most others are CC-BY 4.0, which requires credit. Each `LICENSE.md` carries that credit, so the zip redistributes it with the model.
- **The list:** `content/test_assets.json`, in the repo, lists each model's name, its folder in the zip, what it checks, and whether it should match the reference today. The bundle script and the window both read it. *Why:* one list for both. Editing a note or a status needs no new zip; only adding a model does.
- **Download:** `FetchContent` in a new `content/CMakeLists.txt`, added from the root with `add_subdirectory(content)`, with `URL_HASH SHA256=…`, extracting to `content/downloads/test_assets/` (gitignored). The `NE_TEST_ASSETS` option turns it on; the `debug` and `development` presets set it, `shipping` and CI don't. *Why:* the root `CMakeLists.txt` already gets Slang this way, and FetchContent skips the download when the hash matches. CI doesn't run the app interactively, so the download would only slow its builds.
- **Loading:** a model loads the first time it's picked and stays loaded; the others are hidden. *Why:* `ResourceManager` can't free a mesh or a texture yet, so loading on every pick would use up GPU memory and bindless slots. The bundle is about 30 MB, and loading all of it costs far less than the GPU holds. Freeing resources can come later.

## The Models
About 30 MB in all. Status says whether the model should match the Sample Viewer today; "gap" models show a known missing feature, so the window can show progress on it.

| Model | Checks | Size | Status |
|---|---|---|---|
| MetalRoughSpheresNoTextures | the BRDF; the environment BRDF table | 0.4 MB | match |
| MetalRoughSpheres | the same, from textures | 11.2 MB | match |
| CompareMetallic, CompareRoughness | metallic and roughness ramps | 0.3 MB | match |
| CompareNormal | normal map strength | 0.2 MB | match |
| CompareAmbientOcclusion | occlusion texture | 4.8 MB | match |
| CompareAlphaCoverage | alpha mask and blend | 3.9 MB | match |
| CompareEmissiveStrength, EmissiveStrengthTest | KHR_materials_emissive_strength | 0.1 MB | match |
| AlphaBlendModeTest | alpha modes | 3.0 MB | match |
| NormalTangentTest, NormalTangentMirrorTest | normal maps, with tangents generated and from the file | 3.4 MB | match |
| TextureSettingsTest | samplers (per-axis wrap); double-sided | 0.1 MB | match |
| TextureCoordinateTest | UV orientation | < 0.1 MB | match |
| TextureLinearInterpolationTest, TextureEncodingTest | sRGB vs. linear textures and filtering | 0.1 MB | match |
| OrientationTest | glTF's Y-up turned into the engine's Z-up (REP-103) | 0.1 MB | match |
| TriangleWithoutIndices | unindexed meshes | < 0.1 MB | match |
| CompareBaseColor | base color; uses KHR_texture_transform, which the importer ignores | 1.5 MB | gap |
| MultiUVTest | TEXCOORD_1 | 0.1 MB | gap |
| BoxVertexColors | COLOR_0 | < 0.1 MB | gap |
| NegativeScaleTest | mirrored nodes (winding flips) | 0.1 MB | check |
| MeshPrimitiveModes | points and lines | < 0.1 MB | gap |

Left out for now:
- models that need features the engine doesn't have yet: animation, skins, morph targets, and the KHR_materials_* extensions other than emissive strength;
- KHR_lights_punctual models: DirectionalLight requires the extension, which the importer rejects, and PointLightIntensityTest has nothing to show without lights;
- showcase models such as Sponza (53 MB) and SciFiHelmet (30 MB), which would triple the bundle and check nothing the list above doesn't.

Each can join when its feature lands (`rendering_plan.md`, Later).

## Tasks
1. **Bundle script:** `tools/make_test_assets.cmake`, run with `cmake -P`, so it needs nothing beyond CMake and git on any platform. It:
   - clones the Khronos repo at the pinned commit as a sparse, blob-less clone (`git clone --filter=blob:none --sparse`, then `git sparse-checkout set` with the listed folders), so only those files download;
   - copies each model's `glTF` variant, `LICENSE.md`, and `metadata.json` into a staging folder, plus a `README.md` naming the source commit;
   - zips it (`cmake -E tar cf … --format=zip`) and prints its SHA-256.

   The upload is then one command, `gh release create test-assets-v1 <zip>`, which you run, since it publishes under your account. The SHA-256 goes into `content/CMakeLists.txt`.
2. **Download:** `content/CMakeLists.txt` and the `NE_TEST_ASSETS` option (see Decisions), plus `content/downloads/` in `.gitignore`. The packaged build's `copy_directory` of `content/` skips `downloads/`, so shipping builds stay small.
3. **The window:** "Test Assets" in `MainUI`:
   - lists `content/test_assets.json` by status, with what each model checks; models missing from disk are greyed out, with a hint to configure with `NE_TEST_ASSETS`;
   - picking one hides the helmet and the scene's other objects, places the model at the origin, and frames the camera on its bounds (glTF models range from centimeters to tens of meters);
   - shows the selected model's license line from its `metadata.json`.
4. **Plan update:** `rendering_plan.md`'s Verification Assets section points here, and its "local change to `BasicApp`" step becomes "pick it in the Test Assets window".

## Done when
- A fresh `cmake --preset debug` downloads and extracts the zip once; a second configure doesn't download it again; changing the hash downloads it again.
- The `shipping` preset and CI download nothing.
- Every model in the list loads from the window without validation-layer messages, and the "match" models look like the Sample Viewer's renders with the same tonemapper (PBR Neutral) under a matching sky.

## Later
- **Reference comparison:** the Sample Viewer can render each model with PBR Neutral; saving its renders next to ours with the same camera, and diffing them, turns "looks like" into a number. It needs the same lighting on both sides, so it waits for Phase 7's environment maps. It also needs a headless screenshot mode, which README's Render Views item plans.
- **Freeing models:** once `ResourceManager` can free meshes and textures, the window can unload a model when another is picked.

## Pitfalls
- Some model folders have spaces in their names (`Box With Spaces`); quote paths in the script.
- The `glTF` variant keeps textures as separate files, which the importer reads. Some models also ship `glTF-Embedded` (`data:` URIs), which the importer doesn't read yet; don't pick that variant.
- CC-BY credit must stay with the model: never strip `LICENSE.md` from the zip.
