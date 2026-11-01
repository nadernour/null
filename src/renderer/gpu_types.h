#pragma once

#include "core/math/color.h"
#include "core/math/mat4.h"
#include "core/math/vec2.h"
#include "core/math/vec3.h"
#include "core/math/vec4.h"
#include <cstdint>
#include <type_traits>
#include <volk/volk.h>

// Structs read by the shaders in shaders/; the static_asserts below keep the layouts in sync
namespace ne::gpu {

// The one push-constant range every graphics pipeline shares; Vulkan guarantees 128 bytes
constexpr uint32_t kPushConstantsSize = 128;
constexpr VkShaderStageFlags kPushConstantStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Every push goes through here: it writes the start of the shared range and names all of the range's stages, which
// vkCmdPushConstants requires
template <typename T>
void pushConstants(VkCommandBuffer iCommandBuffer, VkPipelineLayout iLayout, const T& iConstants) {
  static_assert(sizeof(T) <= kPushConstantsSize, "Push constants must fit in the shared range");
  static_assert(std::is_trivially_copyable_v<T>, "Push constants must be trivially copyable, since Vulkan copies their bytes");
  vkCmdPushConstants(iCommandBuffer, iLayout, kPushConstantStages, 0, sizeof(T), &iConstants);
}

// What the scene shader writes: the lit color, or one channel. A channel that holds data (normals, tangents, metallic,
// roughness, occlusion, UVs) reaches the screen as stored, and one that holds a color (base color, emissive) as that
// color. Normal is the normal-mapped normal, Geometry Normal the vertex normal. Cascades is the lit color tinted by shadow
// cascade. shaders/scene.slang mirrors it, so a new view goes in both, and MainUI names it.
enum class DebugView : uint32_t {
  Lit,
  Cascades,
  BaseColor,
  Normal,
  GeometryNormal,
  Tangent,
  Bitangent,
  Metallic,
  Roughness,
  Occlusion,
  Emissive,
  Uv,
  Count
};

struct Vertex {
  Vec3 pos{0.0f};
  Vec3 normal{0.0f, 0.0f, 1.0f};
  Vec2 uv{0.0f};
  LinearColor color = LinearColor::White;
  Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f}; // xyz toward +u; cross(normal, xyz) * w points up the image (toward -v)
};

// Every type takes the same path through the shader. A directional light is a point infinitely far away (position.w =
// 0), so the vector toward it is the same everywhere and has length 1, and the inverse-square falloff divides by 1. A
// light without a range has invRangeSq = 0, and a light without a cone has spotScale = 0 and spotOffset = 1, which turn
// those factors into 1 too.
struct Light {
  Vec4 position{0.0f};     // Point and spot: (world position, 1). Directional: (direction toward the light, 0)
  Vec3 intensity{0.0f};    // Linear color x intensity: lux for directional lights, candela for point and spot lights
  float invRangeSq = 0.0f; // 1 / range^2, 0 without a range
  Vec3 direction{0.0f};    // Where the light travels: its entity's +X
  float spotScale = 0.0f;  // Spot: 1 / max(cos(inner) - cos(outer), 0.001); others: 0
  float spotOffset = 1.0f; // Spot: -cos(outer) * spotScale; others: 1
};

// GlobalUniforms::shadowLight when no light's shadows are drawn, which no light index matches
constexpr uint32_t kNoShadowLight = 0xFFFFFFFFu;

// The cascades of the directional light that casts shadows, which a color view shades with. Cascade i is drawn into
// tile i of the shadow atlas.
struct ShadowData {
  Mat4 cascades[4]{};        // World position -> (atlas u, atlas v, depth) of each cascade
  Vec4 tileBounds[4]{};      // Each cascade's tile as (min u, min v, max u, max v), half a texel in from its edges
  Vec4 depthPlane{0.0f};     // The camera's view depth of a world point p: dot(depthPlane.xyz, p) + depthPlane.w
  Vec4 splitDepths{0.0f};    // The view depth where each cascade ends
  Vec4 normalOffsets{0.0f};  // How far each cascade's lookups move along the normal, in meters
  uint32_t atlasTexture = 0; // Packed like a material's textures: bindless index (low 16 bits), sampler (high 16 bits)
  float texelSize = 0.0f;    // One atlas texel, in atlas UV
};

// What every draw of a view shares: the camera, the lights, the environment, and the shadows
struct GlobalUniforms {
  Mat4 viewProj;
  VkDeviceAddress lights = 0;       // Offset 64, so the addresses stay 8-byte aligned; 0 when lightCount is 0
  VkDeviceAddress shadows = 0;      // ShadowData, read only when shadowLight names a light
  Vec4 eye{0.0f, 0.0f, 0.0f, 1.0f}; // Perspective camera: (position, 1). Orthographic: (backward direction, 0)
  Vec3 skyRadiance{0.0f};           // Nits: color x intensity
  float exposure = 1.0f;            // CameraComponent::getExposure()
  Vec3 groundRadiance{0.0f};        // Nits: color x intensity
  uint32_t lightCount = 0;
  uint32_t debugView = 0;                // DebugView
  uint32_t shadowLight = kNoShadowLight; // The light whose shadows were drawn; kNoShadowLight in depth-only views
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
  uint32_t materialIndex = 0;
};

constexpr uint32_t kMaterialHasNormalTexture = 1u << 0;

// glTF metal-roughness, one per material in ResourceManager's material buffer
struct Material {
  LinearColor baseColor = LinearColor::White;
  Vec3 emissive{0.0f}; // Nits
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
  uint32_t flags = 0; // kMaterialHasNormalTexture
};

// Push constants of shaders/scene.slang
struct ScenePushConstants {
  VkDeviceAddress drawInfos = 0;
  VkDeviceAddress globalUniforms = 0;
  VkDeviceAddress instances = 0;
  VkDeviceAddress materials = 0;
};

// Push constants of shaders/tonemap.slang
struct TonemapPushConstants {
  uint32_t hdrTexture = 0; // Bindless index of the HDR scene color
  uint32_t encodeSrgb = 0; // 0 when the swapchain format encodes sRGB itself
  uint32_t applyCurve = 0; // 0 in debug views, so their values reach the screen unchanged
};

static_assert(sizeof(ScenePushConstants) == 32, "ScenePushConstants size mismatch!");
static_assert(sizeof(TonemapPushConstants) == 12, "TonemapPushConstants size mismatch with Slang layout!");
static_assert(sizeof(Vertex) == 64, "Vertex size mismatch with Slang layout!");
static_assert(sizeof(DrawInfo) == 16, "DrawInfo size mismatch with Slang layout!");
static_assert(sizeof(Light) == 52, "Light size mismatch with Slang layout!");
static_assert(sizeof(GlobalUniforms) == 136, "GlobalUniforms size mismatch with Slang layout!");
static_assert(sizeof(ShadowData) == 376, "ShadowData size mismatch with Slang layout!");
static_assert(sizeof(InstanceData) == 148, "InstanceData size mismatch with Slang layout!");
static_assert(sizeof(Material) == 72, "Material size mismatch with Slang layout!");

static_assert(std::is_standard_layout_v<ScenePushConstants>, "ScenePushConstants must be standard layout!");
static_assert(std::is_standard_layout_v<TonemapPushConstants>, "TonemapPushConstants must be standard layout!");
static_assert(std::is_standard_layout_v<Vertex>, "Vertex must be standard layout!");
static_assert(std::is_standard_layout_v<DrawInfo>, "DrawInfo must be standard layout!");
static_assert(std::is_standard_layout_v<Light>, "Light must be standard layout!");
static_assert(std::is_standard_layout_v<GlobalUniforms>, "GlobalUniforms must be standard layout!");
static_assert(std::is_standard_layout_v<ShadowData>, "ShadowData must be standard layout!");
static_assert(std::is_standard_layout_v<InstanceData>, "InstanceData must be standard layout!");
static_assert(std::is_standard_layout_v<Material>, "Material must be standard layout!");

} // namespace ne::gpu
