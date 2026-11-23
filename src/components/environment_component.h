#pragma once

#include "core/math/math.h"

namespace ne {

// Light from far away, at most one per scene: a sky of one color above the horizon and a ground of one color below
struct EnvironmentComponent {
  LinearColor skyColor = LinearColor::White;
  float skyIntensity = 1.0f; // Nits
  LinearColor groundColor = LinearColor::White;
  float groundIntensity = 1.0f; // Nits
};

} // namespace ne
