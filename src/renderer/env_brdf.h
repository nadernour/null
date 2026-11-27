#pragma once

#include "core/image_data.h"

// The environment BRDF: the share of light arriving evenly from every direction that the specular lobe of the light
// loop's BRDF (GGX, height-correlated Smith, Schlick) reflects toward the camera, as F0 * A + B
namespace ne {

// 64 x 64 RG16F texels of (A, B): NdotV along x and perceptual roughness along y, each taken at its texel's center
ImageData buildBrdfTable();

} // namespace ne
