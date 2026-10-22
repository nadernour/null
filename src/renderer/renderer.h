#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <volk/volk.h>

namespace ne {

class Device;
class FrameManager;
class Image;
class ImGuiManager;
class Instance;
class Registry;
class ResourceManager;
class SceneRenderer;
class Tonemapper;
class Window;

// Owns the GPU side of the engine, from the Vulkan instance to the render targets, and runs the frame. It's the only
// place that orders passes: it records every barrier and opens every pass, and the classes it calls only draw into them.
// The scene renders into the HDR color and depth images, then one swapchain pass holds the tonemap and the UI on top.
class Renderer {
public:
  Renderer(Window* iWindow, const std::string& iAppName);
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  void render(Registry& ioRegistry);
  void waitIdle() const;

  ResourceManager* getResourceManager() const { return mResourceManager.get(); }
  ImGuiManager* getImGuiManager() const { return mImGuiManager.get(); }

private:
  static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

  void createTargets(VkExtent2D iExtent);
  void recordScenePass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent);
  void recordSwapchainPass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent);

  Window* mWindow = nullptr;
  std::unique_ptr<Instance> mInstance;
  std::unique_ptr<Device> mDevice;
  std::unique_ptr<FrameManager> mFrameManager;
  std::unique_ptr<ResourceManager> mResourceManager;
  std::unique_ptr<ImGuiManager> mImGuiManager;

  std::unique_ptr<SceneRenderer> mSceneRenderer;
  std::unique_ptr<Tonemapper> mTonemapper;
  VkFormat mDepthFormat = VK_FORMAT_UNDEFINED;
  bool mEncodeSrgb = true; // False when the swapchain format encodes sRGB itself

  // Swapchain-sized; both frames in flight share them
  std::unique_ptr<Image> mSceneColor;
  std::unique_ptr<Image> mSceneDepth;
  uint32_t mSceneColorTextureIndex = 0; // Bindless slot, registered once and kept across resizes
};

} // namespace ne
