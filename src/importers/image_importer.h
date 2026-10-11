#pragma once

#include "core/image_data.h"

#include <optional>
#include <span>
#include <string>

namespace ne {

namespace ImageImporter {

std::optional<ImageData> importFromFile(const std::string& iPath);
std::optional<ImageData> importFromMemory(std::span<const uint8_t> iData);

} // namespace ImageImporter

} // namespace ne
