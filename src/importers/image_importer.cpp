#include "importers/image_importer.h"
#include "core/assert.h"
#include "core/filesystem.h"
#include "core/logger.h"

#include <stb_image.h>

namespace ne {

namespace {

constexpr int kChannels = 4; // Always load RGBA8 for Vulkan

ImageData copyPixels(const stbi_uc* iPixels, int iWidth, int iHeight) {
  ImageData image{.mWidth = static_cast<uint32_t>(iWidth), .mHeight = static_cast<uint32_t>(iHeight), .mChannels = kChannels};
  image.mPixels.assign(iPixels, iPixels + image.getSizeInBytes());
  return image;
}

} // namespace

std::optional<ImageData> ImageImporter::importFromFile(const std::string& iPath) {
  std::string resolvedPath = ne::fs::resolveContentPath(iPath);

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load(resolvedPath.c_str(), &width, &height, &channels, kChannels);
  if (!pixels) {
    NE_WARN("ImageImporter: Failed to load image from file '{}': {}", resolvedPath, stbi_failure_reason());
    return std::nullopt;
  }

  ImageData image = copyPixels(pixels, width, height);
  stbi_image_free(pixels);
  return image;
}

std::optional<ImageData> ImageImporter::importFromMemory(std::span<const uint8_t> iData) {
  if (iData.empty()) {
    NE_WARN("ImageImporter: Cannot load from empty memory span");
    return std::nullopt;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(iData.data(), static_cast<int>(iData.size()), &width, &height, &channels, kChannels);
  if (!pixels) {
    NE_WARN("ImageImporter: Failed to load image from memory: {}", stbi_failure_reason());
    return std::nullopt;
  }

  ImageData image = copyPixels(pixels, width, height);
  stbi_image_free(pixels);
  return image;
}

} // namespace ne
