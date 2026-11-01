#pragma once

#include "core/math/math.h"
#include <cstdint>

namespace ne {

// KHR_lights_punctual's light types and units. A light shines along its entity's +X, the way a camera looks.
struct LightComponent {
  enum class Type : uint8_t { Directional, Point, Spot };

  Type type = Type::Directional;
  LinearColor color = LinearColor::White;  // Linear; alpha is ignored
  float intensity = 1.0f;                  // Directional: lux. Point and spot: candela
  float range = 0.0f;                      // Point and spot: where the light fades out; 0 for inverse-square only
  float innerConeAngle = 0.0f;             // Spot, radians: full intensity inside it
  float outerConeAngle = math::kPi / 4.0f; // Spot, radians: no light outside it; above the inner angle, at most pi / 2
  bool castShadows = false;                // Directional: the first one that casts shadows gets cascaded shadow maps
};

} // namespace ne
