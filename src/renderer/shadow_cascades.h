#pragma once

#include "core/math/math.h"
#include <array>
#include <cstdint>
#include <optional>

namespace ne {

struct CameraComponent;

// Cascaded shadow maps for one directional light: the camera's view up to a distance, cut into slices by depth, each
// seen from the light through an orthographic view of its own
struct ShadowCascades {
  static constexpr uint32_t kCount = 4;
  // Texels around each slice's bounding sphere, for what reaches past it: snapping (half a texel), the normal offset
  // (1.5), and the 3x3 filter's reach (2)
  static constexpr uint32_t kBorderTexels = 4;

  struct Config {
    float distance = 60.0f;      // View depth where shadows end
    float splitLambda = 0.8f;    // 0 spaces the splits evenly, 1 logarithmically
    float casterMargin = 100.0f; // How far toward the light from a slice casters still cast into it
    uint32_t resolution = 2048;  // Texels along a cascade's side
  };

  std::array<Mat4, kCount> viewProj{};     // World → each cascade's clip space, snapped to whole texels
  std::array<float, kCount> splitDepths{}; // The view depth where each cascade ends
  std::array<float, kCount> texelSizes{};  // The world size of each cascade's texels

  // iLightWorld is the light's world matrix; the light shines along its +X. Empty when nothing is left to shadow: the
  // far plane or the distance is at the near plane.
  static std::optional<ShadowCascades> compute(const CameraComponent& iCamera, const Mat4& iCameraWorld, float iAspectRatio,
                                               const Mat4& iLightWorld, const Config& iConfig);
};

} // namespace ne
