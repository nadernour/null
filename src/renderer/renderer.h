#pragma once

#include <cstdint>
#include <memory>
#include <volk/volk.h>

namespace ne {

class Device;
class FrameRenderer;
class Image;
class ImGuiManager;
class Registry;
class ResourceManager;
class SceneRenderer;
class TonemapPass;

// Runs the frame and is the only place that orders passes: it records every barrier and opens every rendering scope, and
// the passes only draw into them. The scene renders into the HDR color and depth images, then one swapchain scope holds
// the tonemap and the UI on top.
class Renderer {
public:
  static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

  struct Config {
    Device* device = nullptr;
    FrameRenderer* frameRenderer = nullptr;
    ResourceManager* resourceManager = nullptr;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
  };

  Renderer(const Config& iConfig);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  void render(Registry* iRegistry, ImGuiManager* iGuiManager = nullptr);

private:
  void createTargets(VkExtent2D iExtent);
  void recordScenePass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent);
  void recordSwapchainPass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent, ImGuiManager* iGuiManager);

  Device* mDevice = nullptr;
  FrameRenderer* mFrameRenderer = nullptr;
  ResourceManager* mResourceManager = nullptr;
  VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
  bool mEncodeSrgb = true; // False when the swapchain format encodes sRGB itself

  std::unique_ptr<SceneRenderer> mSceneRenderer;
  std::unique_ptr<TonemapPass> mTonemapPass;

  // Swapchain-sized; both frames in flight share them
  std::unique_ptr<Image> mSceneColor;
  std::unique_ptr<Image> mSceneDepth;
  uint32_t mSceneColorTextureIndex = 0; // Bindless slot, registered once and kept across resizes
};

} // namespace ne
