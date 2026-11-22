#pragma once

#include "core/math/math.h"
#include <array>
#include <cstdint>
#include <optional>
#include <volk/volk.h>

namespace ne {

struct CameraComponent;

namespace gpu {
struct ShadowData;
} // namespace gpu

// Cascaded shadow maps for one directional light: the camera's view up to a distance, cut into slices by depth, each
// seen from the light through an orthographic view of its own and drawn into its own tile of one atlas
struct ShadowCascades {
  static constexpr uint32_t kCount = 4;
  static constexpr uint32_t kAtlasSize = 4096;          // Texels along the atlas's side
  static constexpr uint32_t kTileSize = kAtlasSize / 2; // Texels along a cascade's side; cascade i is in tile (i mod 2, i / 2)

  // How far shadow lookups move off the surface, along its normal, in texels of their cascade. Each lookup compares
  // against texels up to two away (nine taps, each blending four texels), so on a surface tilted by an angle a from the
  // light its own stored depth there is up to 2 * tan(a) texels closer to the light than it is. The slope bias
  // (SceneRenderer) clears 1.5 * tan(a) of that, and moving off by d along the normal d / cos(a), so d needs only
  // 0.5 * sin(a): 1.5 covers every angle with room to spare, and gives a surface that faces the light, which has no
  // slope, 1.5 texels of clearance.
  static constexpr float kNormalOffsetTexels = 1.5f;
  // Texels around each slice's bounding sphere, for what reaches past it: snapping (half a texel), the normal offset
  // (kNormalOffsetTexels), and the shader's 3x3 filter (two: one to the outer taps, and one more that each tap blends
  // in). Each light view's near plane lies on its tile's box, so the border also keeps every lookup behind that plane.
  static constexpr uint32_t kBorderTexels = 4;

  struct Config {
    float distance = 60.0f;   // View depth where shadows end
    float splitLambda = 0.8f; // 0 spaces the splits evenly, 1 logarithmically
  };

  std::array<Mat4, kCount> viewProj{};     // World → each cascade's clip space, snapped to whole texels
  std::array<float, kCount> splitDepths{}; // The view depth where each cascade ends
  std::array<float, kCount> texelSizes{};  // The world size of each cascade's texels
  Vec4 depthPlane{0.0f}; // The camera's view depth of a world point p, which splitDepths measure: dot(xyz, p) + w

  // iLightWorld is the light's world matrix; the light shines along its +X. Empty when nothing is left to shadow: the
  // far plane or the distance is at the near plane.
  static std::optional<ShadowCascades> compute(const CameraComponent& iCamera, const Mat4& iCameraWorld, float iAspectRatio,
                                               const Mat4& iLightWorld, const Config& iConfig);

  // Where cascade iCascade is drawn in the atlas
  static VkRect2D getTile(uint32_t iCascade);

  // What color views shade with: each cascade's matrix into its tile of the atlas, where it ends, and how far its lookups
  // move off the surface. iAtlasTexture is the atlas's bindless index.
  gpu::ShadowData makeShadowData(uint32_t iAtlasTexture) const;
};

} // namespace ne
