#pragma once

#include "core/math/color.h"
#include "core/math/mat4.h"
#include "core/math/vec2.h"
#include "core/math/vec3.h"
#include <cstdint>
#include <type_traits>
#include <volk/volk.h>

// Structs read by the shaders in shaders/; the static_asserts below keep the layouts in sync
namespace ne::gpu {

// The one push-constant range every graphics pipeline shares; Vulkan guarantees 128 bytes. vkCmdPushConstants passes
// exactly these stages, since it must name every stage of the range it writes into.
constexpr uint32_t kPushConstantsSize = 128;
constexpr VkShaderStageFlags kPushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

struct Vertex {
  Vec3 pos{0.0f};
  Vec3 normal{0.0f, 0.0f, 1.0f};
  Vec2 uv{0.0f};
  LinearColor color = LinearColor::White;
};

struct GlobalUniforms {
  Mat4 viewProj;
};

// One per indirect draw, indexed by SV_DrawIndex
struct DrawInfo {
  VkDeviceAddress vertices = 0;
  uint32_t instanceBaseOffset = 0;
};

struct InstanceData {
  Mat4 modelMatrix;
  Mat4 normalMatrix;
  LinearColor color = LinearColor::White;
  uint32_t textureIndex = 0;
  uint32_t samplerIndex = 0;
};

// Push constants of shaders/base_shader.slang
struct ScenePushConstants {
  VkDeviceAddress drawInfos = 0;
  VkDeviceAddress globalUniforms = 0;
  VkDeviceAddress instances = 0;
};

// Push constants of shaders/tonemap.slang
struct TonemapPushConstants {
  uint32_t hdrTexture = 0; // Bindless index of the HDR scene color
  uint32_t encodeSrgb = 0; // 0 when the swapchain format encodes sRGB itself
};

static_assert(sizeof(ScenePushConstants) <= kPushConstantsSize, "ScenePushConstants exceeds the shared push constant range!");
static_assert(sizeof(TonemapPushConstants) <= kPushConstantsSize, "TonemapPushConstants exceeds the shared push constant range!");
static_assert(sizeof(ScenePushConstants) == 24, "ScenePushConstants size mismatch!");
static_assert(sizeof(TonemapPushConstants) == 8, "TonemapPushConstants size mismatch with Slang layout!");
static_assert(sizeof(Vertex) == 48, "Vertex size mismatch with Slang layout!");
static_assert(sizeof(DrawInfo) == 16, "DrawInfo size mismatch with Slang layout!");
static_assert(sizeof(GlobalUniforms) == 64, "GlobalUniforms size mismatch!");
static_assert(sizeof(InstanceData) == 152, "InstanceData size mismatch with Slang layout!");

static_assert(std::is_standard_layout_v<ScenePushConstants>, "ScenePushConstants must be standard layout!");
static_assert(std::is_standard_layout_v<TonemapPushConstants>, "TonemapPushConstants must be standard layout!");
static_assert(std::is_standard_layout_v<Vertex>, "Vertex must be standard layout!");
static_assert(std::is_standard_layout_v<DrawInfo>, "DrawInfo must be standard layout!");
static_assert(std::is_standard_layout_v<GlobalUniforms>, "GlobalUniforms must be standard layout!");
static_assert(std::is_standard_layout_v<InstanceData>, "InstanceData must be standard layout!");

} // namespace ne::gpu
