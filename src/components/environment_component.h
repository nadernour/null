#pragma once

#include "core/math/math.h"

namespace ne {

// The light that reaches the scene from far away, at most one per scene: until Image-Based Lighting, a sky of one color
// above the horizon and a ground of one color below it
struct EnvironmentComponent {
  LinearColor skyColor = LinearColor::White;
  float skyIntensity = 1.0f; // Nits
  LinearColor groundColor = LinearColor::White;
  float groundIntensity = 1.0f; // Nits
};

} // namespace ne
