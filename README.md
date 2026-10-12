# Null Engine

A digital twin platform for robot simulation, built on a Vulkan 1.4 renderer written in C++20. The goal is an easy way to build virtual robots and their environments, simulate them with realistic sensors, train robot behaviors, and connect the results to real robots. Design choices favor ROS conventions, headless operation, and sensor simulation.

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?logo=cplusplus&logoColor=white&style=flat-square" alt="C++ Standard"></a>
  <a href="https://www.vulkan.org/"><img src="https://img.shields.io/badge/Vulkan-1.4-red.svg?logo=vulkan&logoColor=white&style=flat-square" alt="Vulkan Version"></a>
  <a href="https://shader-slang.com/"><img src="https://img.shields.io/badge/Shader%20Language-Slang-orange?style=flat-square" alt="Slang Shaders"></a>
  <a href="https://ninja-build.org/"><img src="https://img.shields.io/badge/Build%20System-Ninja-yellow?style=flat-square" alt="Ninja Generator"></a>
  <a href="CMakePresets.json"><img src="https://img.shields.io/badge/CMake-Presets-green?logo=cmake&style=flat-square" alt="CMake Presets"></a>
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey?style=flat-square" alt="Platforms">
</p>

---

## 📌 Roadmap & Task Board

The sections below group work by area; their order is not fixed. The open decisions are left for when the platform's first use cases are clear.

### Open Decisions
- **Physics**: A built-in engine, a physics library inside Null (MuJoCo, PhysX, Jolt, ...), or an external simulator streaming its state in.
- **Robot & World Formats**: Which of URDF, MJCF, SDF, and USD to support, and whether Null gets its own scene format.
- **Connectivity**: How Null talks to real robots and other tools (ROS 2, Zenoh, ...).
- **Training Interface**: How learning code drives Null (Python bindings, a Gym-style API, a network protocol, ...).

### Foundations
- [x] **Debug Utils Instrumentation**: Name every Vulkan object for RenderDoc and validation messages.
- [x] **Vulkan 1.3/1.4 Memory2 & Transfer Core**: Memory binding and buffer copies use the newer `*2` Vulkan APIs.
- [x] **Asset Pipeline**: Load glTF / GLB models straight into GPU memory.
- [x] **Input Abstraction**: Typed keyboard and mouse input with multicast events.
- [x] **Camera & Transform Math**: Perspective and orthographic projections, and position / rotation / scale transforms.
- [x] **Scene & ECS Integration**: Cameras and transforms are ECS components drawn by the renderer.
- [x] **Reverse-Z Pipeline Integration**: Reverse-Z depth for better precision at a distance.
- [x] **CameraController**: Free-fly camera (WASD + QE, right-click to look).
- [x] **Dear ImGui Overlay**: Diagnostics, frame stats, and camera controls.
- [x] **Simulation Time Controls & UI Toggle**: Time scale (0-3x), UI blocks 3D input while in use, `H` toggles the overlay.
- [x] **Right-Handed Z-Up Convention (ROS REP-103)**: glTF, URDF, ROS, and USD data import without mirroring.
- [x] **Transform Hierarchy & glTF Scene Import**: Parent / child transforms, glTF node trees, and reusable prefabs.
- [x] **Transient Per-Draw Data Core**: Per-draw data reaches shaders through GPU pointers, drawn with multi-draw indirect.
- [x] **Vertex Attribute Modernization**: Position, normal, UV, and color, fetched by the vertex shader itself.
- [x] **RHI Texture & Sampler Core**: Textures, staging uploads, layout transitions, and samplers.
- [x] **Vulkan Feature Enablement**: Anisotropic filtering and descriptor indexing, checked against hardware limits.
- [x] **Bindless Texture Architecture**: All textures live in one shader array, indexed per draw.
- [x] **Vulkan Memory Allocator (VMA) Integration**: Every buffer and image is sub-allocated through VMA.
- [x] **Mipmap Generation**: Every texture gets a full mip chain, built on the CPU at load with an sRGB-correct box filter.

### Hardening
- [x] **Self-Contained Vulkan Build**: Vulkan headers, volk, and VMA live in `third_party`, so the C++ build needs no Vulkan SDK.
- [x] **Downloaded Shader Compiler**: CMake downloads Slang, so building needs no Vulkan SDK.
- [x] **In-House Logger**: Replaces spdlog, with colored console output and one log file per run (the last 10 are kept).
- [x] **Up-to-Date Third-Party Libraries**: GLFW, Dear ImGui, cgltf, and stb updated to their latest versions, with each library's source and version recorded.
- [x] **Consistent Naming**: Types with private state are `class`es with `m`-prefixed members; plain data types (all fields public, nothing to protect) are `struct`s with plain camelCase fields. Every parameter, lambdas included, has an `i` / `o` / `io` prefix.
- [x] **Mesh Index Validation**: A mesh whose indices run past its vertices is rejected with a logged reason instead of making the GPU read another mesh's memory: `ResourceManager::createMesh` runs `MeshData::validate` on every mesh, and glTF files also pass `cgltf_validate`.
- [ ] **Logging in Shipping Builds**: Shipping keeps its log output, and a failed Vulkan call logs what failed before aborting, so a headless run that dies leaves a reason behind.
- [ ] **Bad Input Is an Error, Not an Assert**: Asserts catch only engine bugs; bad input is rejected with a logged reason in every build. Left: glTF accessor reads, geometry pool overflow (until Reusable Geometry Pools grows the pools), and the bindless texture limit.
- [ ] **Drift-Free Clock**: Total time accumulates in integer nanoseconds, so long runs don't drift.
- [ ] **In-App Log Panel**: Recent log lines in an ImGui window, filtered by level at runtime.

### Robots & Worlds
- [ ] **URDF Import**: Robot links and joints (fixed, revolute, continuous, prismatic, and mimic) as a transform hierarchy, with several visuals per link and `package://` mesh paths.
- [ ] **Mesh Formats**: STL, OBJ, and DAE next to glTF, with duplicate vertices merged and each format's units and up axis respected.
- [ ] **Primitive Meshes**: Cylinder, sphere, and capsule next to the existing box.
- [ ] **Robot Materials**: Base colors and textures per submesh, from robot descriptions and glTF.
- [ ] **World Scenes**: Environments of static meshes, robots, sensors, and lights, loaded from a file.

### Simulation
- [ ] **Simulation Clock**: Sim time stepped at a fixed rate, separate from the frame rate, and able to run faster or slower than real time.
- [ ] **Physics**: Rigid bodies, joints, contacts, and actuators, from the source chosen in the physics decision.
- [ ] **Robot Control**: Command joints by position, velocity, or effort, and read back joint states.
- [ ] **Determinism**: The same inputs and seed give the same results, so experiments and tests can be repeated.
- [ ] **Scene Extraction**: Copy cameras and draw lists out of the ECS before rendering, so the renderer stops reading components directly and simulation and rendering can run at different rates.

### Sensors
- [ ] **Frame Orchestration & UI Pass**: One place runs the whole frame; the UI gets its own pass after the scene and tonemapping.
- [ ] **Render Views & Offscreen Targets**: Several cameras rendering offscreen at once, headless mode, GPU → CPU readback, and an ImGui viewport panel.
- [ ] **Camera Sensors**: RGB, depth, and segmentation images from cameras mounted on robot frames, with intrinsics and lens distortion.
- [ ] **Ray-Query LiDAR**: Hardware ray tracing for LiDAR and depth sensors, with a fallback for GPUs without ray tracing.
- [ ] **Sensor Noise**: Configurable noise and artifacts, so behavior trained in simulation holds up on real sensors.
- [ ] **glTF PBR Material Pipeline**: glTF metal-roughness materials with textures, transparency, and tonemapping (per-instance material index, MikkTSpace tangents, Khronos PBR Neutral).

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

### Engine & Performance
- [ ] **CI**: Build and run the tests on every push, on lavapipe (Mesa's software Vulkan).
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

---

## 🛠️ Building & Testing

### Requirements
* **OS**: Linux or Windows 10 / 11.
* **Compiler**: C++20 with `<format>` and time zones: GCC 14+ or MSVC 2022.
* **Build Tools**: CMake 3.24+ and Ninja.
* **Shader Compiler**: Nothing to install: the first configure of each build folder downloads Slang.
* **Validation Layers** (optional, Debug builds): From the Vulkan SDK or a distro package such as `vulkan-validationlayers`; without them, Debug builds run unvalidated.
* **GPU**: A Vulkan 1.3 driver and loader (`libvulkan1` on Debian / Ubuntu) with dynamic rendering, synchronization2, buffer device address, descriptor indexing, and multi-draw indirect.

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

---

## 📄 License

This project is licensed under the MIT License.
