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

// Cascaded shadow maps for one directional light: the camera's view up to its shadow distance, cut into kCount slices by
// depth, each drawn from the light into its own tile of one atlas
struct ShadowCascades {
  static constexpr uint32_t kCount = 4;
  static constexpr uint32_t kAtlasSize = 4096;          // Texels per side
  static constexpr uint32_t kTileSize = kAtlasSize / 2; // Texels per side; cascade i is in tile (i mod 2, i / 2)

  // How far shadow lookups move off the surface along its normal, in texels of their cascade: on a surface tilted by a, it
  // lifts them 1.5 / cos(a) texels of depth. With the slope bias's 1.5 tan(a), that clears the 2.83 tan(a) the 3x3 PCF
  // reaches toward the light (two texels on both axes) at any tilt; grazing tilts need the two factors to sum past 2.83.
  static constexpr float kNormalOffsetTexels = 1.5f;
  // The casters' slope bias, negative for reverse-Z; the normal offset does a constant bias's job, and an even split with
  // it keeps steep casters' shadows attached. Vulkan lets drivers take the slope on the steeper axis alone, which makes
  // this 1.06 tan(a) on a diagonal slope: lookups then clear to 58 degrees of tilt, past which under 0.5% of the PCF fails.
  static constexpr float kDepthBiasSlope = -1.5f;
  // Texels left around each slice for what reaches past its bounding sphere: snapping (0.5), the normal offset (1.5), and
  // the 3x3 PCF (2). It also keeps every lookup behind the light view's near plane, which lies on the tile's box.
  static constexpr uint32_t kBorderTexels = 4;
  // Where the splits lie between even spacing (0) and logarithmic (1)
  static constexpr float kSplitLambda = 0.8f;

  std::array<Mat4, kCount> viewProj{};     // World → each cascade's clip space, snapped to whole texels
  std::array<float, kCount> splitDepths{}; // The view depth where each cascade ends
  std::array<float, kCount> texelSizes{};  // Meters
  Vec4 depthPlane{0.0f}; // The camera's view depth of a world point p, which splitDepths measure: dot(xyz, p) + w

  // Empty when the shadow distance or the far plane doesn't reach past the near plane
  static std::optional<ShadowCascades> compute(const CameraComponent& iCamera, const Mat4& iCameraWorld, float iAspectRatio,
                                               const Mat4& iLightWorld);

  static VkRect2D getTile(uint32_t iCascade);

  // What color views shade with; iAtlasTexture is the atlas's bindless index
  gpu::ShadowData makeShadowData(uint32_t iAtlasTexture) const;
};

} // namespace ne
