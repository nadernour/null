# Null Engine

A digital twin platform for robot simulation, built on a Vulkan 1.4 renderer written in C++20. The goal is an easy way to build virtual robots and their environments, simulate them with realistic sensors, train robot behaviors, and connect the results to real robots. Design choices favor ROS conventions, headless operation, and sensor simulation.

<p align="center">
  <a href="https://github.com/nadernour/null/actions/workflows/ci.yml"><img src="https://img.shields.io/github/actions/workflow/status/nadernour/null/ci.yml?branch=main&label=CI&logo=github&style=flat-square" alt="CI Status"></a>
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?logo=cplusplus&logoColor=white&style=flat-square" alt="C++ Standard"></a>
  <a href="https://www.vulkan.org/"><img src="https://img.shields.io/badge/Vulkan-1.4-red.svg?logo=vulkan&logoColor=white&style=flat-square" alt="Vulkan Version"></a>
  <a href="https://shader-slang.com/"><img src="https://img.shields.io/badge/Shader%20Language-Slang-orange?style=flat-square" alt="Slang Shaders"></a>
  <a href="https://ninja-build.org/"><img src="https://img.shields.io/badge/Build%20System-Ninja-yellow?style=flat-square" alt="Ninja Generator"></a>
  <a href="CMakePresets.json"><img src="https://img.shields.io/badge/CMake-Presets-green?logo=cmake&style=flat-square" alt="CMake Presets"></a>
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey?style=flat-square" alt="Platforms">
</p>

---

## ✅ What Works Today

- **Rendering**: Vulkan 1.3 / 1.4 with dynamic rendering and synchronization2. The scene renders into its own HDR target, which a tonemapper (Khronos PBR Neutral) writes to the window, with the UI drawn on top. The vertex shader fetches vertices and per-draw data through GPU pointers, and draws go out as multi-draw indirect. Materials are GPU data: one buffer of glTF metal-roughness materials, indexed per instance, whose base color, metallic-roughness, normal, occlusion, and emissive textures each keep their own sampler settings; debug views show each channel as the Khronos glTF Sample Viewer does. All textures live in one bindless array with full mip chains (built on the CPU with an sRGB-correct box filter), every buffer and image comes from VMA, depth is reverse-Z, and every Vulkan object is named for RenderDoc and validation messages.
- **Assets**: glTF / GLB models, whose node trees become reusable prefabs with their materials and textures, and PNG / JPEG textures.
- **Scene & Input**: An ECS with parent / child transforms, perspective and orthographic cameras, and a free-fly camera controller (WASD + QE, right-click to look) on typed keyboard and mouse input.
- **Tools**: A Dear ImGui overlay with frame stats, camera settings, and a simulation time scale (0-3x); it blocks 3D input while in use, and `H` toggles it. A logger with colored console output keeps one log file per run (the last 10).
- **Build & CI**: No Vulkan SDK needed: Vulkan headers, volk, and VMA are vendored, and CMake downloads the Slang compiler. Every push and pull request builds all three presets on Linux (GCC 14, Debian 13) and Windows (MSVC), and runs the tests on Linux on lavapipe (Mesa's software Vulkan) under Xvfb.

---

## 📌 Roadmap & Task Board

The sections below group work by area; their order is not fixed. The open decisions are left for when the platform's first use cases are clear.

### Open Decisions
- **Physics**: A built-in engine, a physics library inside Null (MuJoCo, PhysX, Jolt, ...), or an external simulator streaming its state in.
- **Robot & World Formats**: Which of URDF, MJCF, SDF, and USD to support, and whether Null gets its own scene format.
- **Connectivity**: How Null talks to real robots and other tools (ROS 2, Zenoh, ...).
- **Training Interface**: How learning code drives Null (Python bindings, a Gym-style API, a network protocol, ...).

### Rendering
The steps are in order, each building on the one before; they bring physically based rendering to the glTF and robot materials that camera sensors will see. [rendering_plan.md](rendering_plan.md) has each step's decisions and tasks.
- [x] **HDR Scene Target & Frame Orchestration**: The scene renders into its own HDR color and depth images instead of the swapchain, a tonemapper (Khronos PBR Neutral) writes the result to the window, and the UI draws on top in its own step afterwards. One place runs the whole frame: scene, tonemap, UI.
- [x] **Materials & glTF Textures**: Materials become GPU data, a material buffer indexed per instance, with the glTF metal-roughness fields (base color, metallic, roughness, normal, occlusion, and emissive, each with an optional texture). glTF materials and textures are imported instead of one shared default.
- [ ] **Tangents, Normal Maps & Alpha**: Tangents from glTF or generated with MikkTSpace, normal mapping, alpha mask and blend, and double-sided materials.
- [ ] **Lights, Exposure & PBR Shading**: Lights become ECS components (directional, point, and spot, in lux and candela) and replace the light fixed in the shader. The glTF metal-roughness BRDF (GGX, Smith, Schlick), camera exposure from aperture, shutter speed, and ISO, and a sky / ground ambient that metals reflect.
- [ ] **Shadows**: The scene's draws are gathered once per frame and recorded per view, then cascaded shadow maps for the directional light; spot and point light shadows later.
- [ ] **Image-Based Lighting**: HDR environment maps light and reflect the scene (prefiltered specular, spherical-harmonics diffuse) and are drawn as the sky.

### Robots & Worlds
- [ ] **URDF Import**: Robot links and joints (fixed, revolute, continuous, prismatic, and mimic) as a transform hierarchy, with several visuals per link and `package://` mesh paths.
- [ ] **Mesh Formats**: STL, OBJ, and DAE next to glTF, with duplicate vertices merged and each format's units and up axis respected.
- [ ] **Primitive Meshes**: Cylinder, sphere, and capsule next to the existing box.
- [ ] **Robot Materials**: Colors and textures from robot descriptions (URDF, MJCF, ...), mapped onto the metal-roughness materials from Materials & glTF Textures.
- [ ] **World Scenes**: Environments of static meshes, robots, sensors, and lights, loaded from a file.

### Simulation
- [ ] **Simulation Clock**: Sim time stepped at a fixed rate, separate from the frame rate, and able to run faster or slower than real time.
- [ ] **Physics**: Rigid bodies, joints, contacts, and actuators, from the source chosen in the physics decision.
- [ ] **Robot Control**: Command joints by position, velocity, or effort, and read back joint states.
- [ ] **Determinism**: The same inputs and seed give the same results, so experiments and tests can be repeated.
- [ ] **Scene Extraction**: Copy cameras and draw lists out of the ECS before rendering, so the renderer stops reading components directly and simulation and rendering can run at different rates.

### Sensors
- [ ] **Render Views & Offscreen Targets**: Several cameras rendering offscreen at once, each into its own HDR scene target, headless mode (so CI's tests no longer need Xvfb), GPU → CPU readback, and an ImGui viewport panel.
- [ ] **Camera Sensors**: RGB, depth, and segmentation images from cameras mounted on robot frames, with intrinsics and lens distortion.
- [ ] **Ray-Query LiDAR**: Hardware ray tracing for LiDAR and depth sensors, with a fallback for GPUs without ray tracing.
- [ ] **Sensor Noise**: Configurable noise and artifacts, so behavior trained in simulation holds up on real sensors.

### Training
- [ ] **Parallel Environments**: Many copies of a scene stepping and rendering at once on one GPU.
- [ ] **Episodes & Resets**: Fast resets to a start state, with randomized poses, materials, lighting, and physics parameters.
- [ ] **Training API**: Observations, actions, and rewards exposed to learning code through the chosen training interface.
- [ ] **Datasets**: Record sensor data and robot states for imitation learning and synthetic data.

### Real Robots
- [ ] **Robot Connection**: Stream joint states and poses from a real robot or another simulator into the scene, timestamped and interpolated, through the chosen connectivity.
- [ ] **Record & Replay**: Record sessions and replay them for debugging and tests.
- [ ] **Sim vs Real View**: The simulated and the real robot shown together, to spot where they diverge.

### Tools
- [ ] **Debug Draw**: Lines, coordinate frames, joint axes, trajectories, and point clouds.
- [ ] **Picking & Gizmos**: Select entities and move them in the viewport.
- [ ] **Scene Editor**: Place robots, objects, and sensors in the viewport and save the scene.
- [ ] **In-App Log Panel**: Recent log lines in an ImGui window, filtered by level at runtime, for runs without a terminal.

### Engine & Performance
- [ ] **Windows Tests in CI**: Run the tests on Windows too, on Mesa's Windows build of lavapipe.
- [ ] **GPU Timeline & Deferred Destruction**: Free GPU resources only once the GPU is done with them, and stop uploads from stalling (one timeline semaphore on `Device` plus a deletion queue).
- [ ] **Reusable Geometry Pools**: Vertex and index pools that free and reuse space (VMA virtual blocks) and grow on demand, so robots can be unloaded and large meshes fit.
- [ ] **Resource Handles**: Meshes and materials (now `std::shared_ptr`) and textures (now bare bindless indices) are referred to by generational handles (like `Entity`), owned by whatever loaded them (a prefab, a world) and freed through the deferred-destruction queue, so components stay plain data. Lands with GPU Timeline & Deferred Destruction and Reusable Geometry Pools.
- [ ] **Core Library**: The engine as a library, with separate apps on top (viewer, headless runner, tests).
- [ ] **Compute Frustum & Occlusion Culling**: Cull on the GPU and build draw commands in compute shaders (Hi-Z depth pyramid from a compute downsampler, with per-mip views and barriers).
- [ ] **Texture Streaming**: Compressed (KTX) textures loaded in the background (staging ring buffer, reusable bindless slots).
- [ ] **GPU Profiling**: GPU timings per render pass.
- [ ] **Push Descriptors for Utility Passes**: Simple post-processing and compute passes without descriptor pools.
- [ ] **Context-Driven Encoder Pattern & RHI Decoupling**: A clean boundary between the low-level Vulkan layer and the render passes.

---

## 📐 Engine Conventions

* **Coordinates**: ROS REP-103: right-handed, `+X` forward, `+Y` left, `+Z` up; roll / pitch / yaw follow ROS (`Rz * Ry * Rx`).
* **Camera View Space**: The ROS optical frame (`+X` right, `+Y` down, `+Z` forward), which matches Vulkan NDC.
* **Units**: SI (meters, seconds), angles in radians.
* **Depth**: Reverse-Z: the near plane maps to 1 and the far plane to 0, for precision at a distance.
* **Time**: Integer nanoseconds (`Time::getTime`, int64 like ROS 2 time), converted to seconds only on read, so long runs don't drift.
* **Naming**: Types with private state are `class`es with `m`-prefixed members; plain data types (all fields public, nothing to protect) are `struct`s with plain camelCase fields. Every parameter, lambdas included, has an `i` / `o` / `io` prefix.
* **Errors**: Asserts catch only engine bugs. Bad input is rejected with a logged reason in every build: glTF files that require an unsupported extension, mistyped or unreadable attributes, meshes whose indices run past their vertices or that don't fit the geometry pools, and textures past the GPU's size limit or the bindless table. glTF textures the importer can't honor (on a UV set other than the first, or in an image stb can't decode) are dropped with a warning, and the material uses white instead. A failed Vulkan call logs what failed before aborting, Shipping included.
* **Third-Party Code**: Vendored in `third_party/`, with each library's source and version recorded.

---

## 🛠️ Building & Testing

### Requirements
* **OS**: Linux or Windows 10 / 11.
* **Compiler**: C++20 with `<format>` and time zones: GCC 14+ or MSVC 2022.
* **Build Tools**: CMake 3.24+ and Ninja.
* **Shader Compiler**: Nothing to install: the first configure of each build folder downloads Slang.
* **Validation Layers** (optional, Debug builds): From the Vulkan SDK or a distro package such as `vulkan-validationlayers`; without them, Debug builds run unvalidated.
* **GPU**: A Vulkan 1.3 driver and loader (`libvulkan1` on Debian / Ubuntu) with dynamic rendering, synchronization2, buffer device address, descriptor indexing, and multi-draw indirect. Mesa's lavapipe (software Vulkan) works too.

### Linux
```bash
# Configure preset (debug | development | shipping) - first time only
cmake --preset=debug

# Build & Run Unit Tests (Single Command)
cmake --build --preset debug && ./build/debug/bin/null_engine --run-tests
```

### Windows (PowerShell)
```powershell
# Build & Run Unit Tests (Single Command)
.\build.ps1 -Preset debug; .\build\debug\bin\null_engine.exe --run-tests
```

> **Packaged Builds**: Configure with `-DNE_PACKAGED_BUILD=ON` to copy assets alongside the output binary.

> **Verbose Logging**: Every allocation and subsystem step is always written to the run's log file (`logs/` next to the executable). Pass `--verbose` (or `-v`) to also show them on the console; it is set at startup for the whole run.
