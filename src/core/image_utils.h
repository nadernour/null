#pragma once

#include "core/image_data.h"

namespace ne::ImageUtils {

// Levels in a full chain down to 1x1
uint32_t calculateMipLevels(uint32_t iWidth, uint32_t iHeight);

// Copies level 0 of an RGBA8 image and box-filters every smaller level from the one above it, in linear light when iSrgb
ImageData generateMipChain(const ImageData& iImage, bool iSrgb);

} // namespace ne::ImageUtils
