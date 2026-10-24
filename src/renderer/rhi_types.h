#pragma once

#include <cstdint>

namespace ne {

// Opaque handle to a memory allocation owned by MemoryAllocator. Only MemoryAllocator interprets it.
struct MemoryAllocation_T;
using MemoryAllocation = MemoryAllocation_T*;

// Access pattern of an allocation; MemoryAllocator picks the memory type
enum class MemoryUsage : uint8_t {
  DeviceLocal, // Device-local VRAM (vertex, index, storage)
  Upload,      // Host-visible upload / staging / uniform (writes to ReBAR VRAM or RAM)
  Readback     // Readback / profiling
};

} // namespace ne
