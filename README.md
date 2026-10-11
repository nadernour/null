# Null Engine

A high-performance, modular 3D rendering engine built with modern C++20 and Vulkan 1.4. It is heading toward a robotics digital twin (see Step 5), so design choices favor ROS conventions, headless rendering, and sensor simulation.

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-blue.svg?logo=cplusplus&logoColor=white&style=flat-square" alt="C++ Standard"></a>
  <a href="https://www.vulkan.org/"><img src="https://img.shields.io/badge/Vulkan-1.4-red.svg?logo=vulkan&logoColor=white&style=flat-square" alt="Vulkan Version"></a>
  <a href="https://shader-slang.com/"><img src="https://img.shields.io/badge/Shader%20Language-Slang-orange?style=flat-square" alt="Slang Shaders"></a>
  <a href="https://ninja-build.org/"><img src="https://img.shields.io/badge/Build%20System-Ninja-yellow?style=flat-square" alt="Ninja Generator"></a>
  <a href="CMakePresets.json"><img src="https://img.shields.io/badge/CMake-Presets-green?logo=cmake&style=flat-square" alt="CMake Presets"></a>
  <img src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey?style=flat-square" alt="Platforms">
</p>

---

## 📌 Engine Roadmap & Task Board

### Step 1: Foundational Modernization & RHI Core
- [x] **Debug Utils Instrumentation**: Name every Vulkan object for RenderDoc and validation messages.
- [x] **Vulkan 1.3/1.4 Memory2 & Transfer Core**: Memory binding and buffer copies use the newer `*2` Vulkan APIs.

### Step 2: Interactive Camera & GUI
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

### Step 3: Materials & Bindless Resources
- [x] **Transient Per-Draw Data Core**: Per-draw data reaches shaders through GPU pointers, drawn with multi-draw indirect.
- [x] **Vertex Attribute Modernization**: Position, normal, UV, and color, fetched by the vertex shader itself.
- [x] **RHI Texture & Sampler Core**: Textures, staging uploads, layout transitions, and samplers.
- [x] **Vulkan Feature Enablement**: Anisotropic filtering and descriptor indexing, checked against hardware limits.
- [x] **Bindless Texture Architecture**: All textures live in one shader array, indexed per draw.
- [x] **Vulkan Memory Allocator (VMA) Integration**: Every buffer and image is sub-allocated through VMA.
- [x] **Mipmap Generation**: Every texture gets a full mip chain, built on the CPU at load with an sRGB-correct box filter.
- [ ] **glTF PBR Material Pipeline**: glTF metal-roughness materials with textures, transparency, and tonemapping (per-instance material index, MikkTSpace tangents, Khronos PBR Neutral).
- [ ] **GPU Timeline & Deferred Destruction**: Free GPU resources only once the GPU is done with them, and stop uploads from stalling (one timeline semaphore on `Device` plus a deletion queue; no resource handles until something needs them).

### Step 4: GPU-Driven Pipeline & Optimization
- [ ] **Scene Extraction**: Copy cameras and draw lists out of the ECS before rendering, so the renderer stops reading components directly.
- [ ] **Frame Orchestration & UI Pass**: One place runs the whole frame; the UI gets its own pass after the scene and tonemapping.
- [ ] **Render Views & Offscreen Targets**: Several cameras rendering offscreen at once, headless mode, GPU → CPU readback, and an ImGui viewport panel.
- [ ] **Push Descriptors for Utility Passes**: Simple post-processing and compute passes without descriptor pools.
- [ ] **GPU Profiling**: GPU timings per render pass.
- [ ] **Context-Driven Encoder Pattern & RHI Decoupling**: A clean boundary between the low-level Vulkan layer and the render passes.
- [ ] **Compute Frustum & Occlusion Culling**: Cull on the GPU and build draw commands in compute shaders (Hi-Z depth pyramid from a compute downsampler, with per-mip views and barriers).
- [ ] **Texture Streaming**: Compressed (KTX) textures loaded in the background (staging ring buffer, reusable bindless slots).

### Step 5: Robotics Digital Twin (Future)
> Null stays the renderer, visualizer, and sensor simulator. Physics comes from an external source of truth (MuJoCo, Isaac Sim, Gazebo, or the real robot).
- [ ] **URDF Import**: Robot links and joints as a transform hierarchy, with STL / DAE / OBJ meshes.
- [ ] **Debug Draw**: Lines, coordinate frames, trajectories, and point clouds.
- [ ] **External State Bridge**: A ROS 2 / Zenoh bridge, outside the engine core, that streams timestamped, interpolated joint states and poses into the scene.
- [ ] **Camera Sensor Simulation**: RGB, depth, and segmentation images from simulated cameras.
- [ ] **Picking & Gizmos**: Select entities and move them in the viewport.
- [ ] **Ray-Query LiDAR**: Hardware ray tracing for LiDAR and depth sensors.

---

## 📐 Engine Conventions

* **Coordinates**: ROS REP-103: right-handed, `+X` forward, `+Y` left, `+Z` up; roll / pitch / yaw follow ROS (`Rz * Ry * Rx`).
* **Units**: SI (meters, seconds), angles in radians.

---

## 🛠️ Building & Testing

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
