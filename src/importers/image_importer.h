#pragma once

#include "core/image_data.h"

#include <optional>
#include <span>
#include <string>

namespace ne {

namespace image_importer {

std::optional<ImageData> importFromFile(const std::string& iPath);
std::optional<ImageData> importFromMemory(std::span<const uint8_t> iData);

} // namespace image_importer

} // namespace ne
