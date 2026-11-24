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
struct SceneView;
struct ShadowCascades;
class Tonemapper;
class Window;

namespace gpu {
enum class DebugView : uint32_t;
}

// Owns the GPU side, from the Vulkan instance to the render targets, and runs the frame. The only place that orders and
// opens passes and records barriers; the classes it calls only draw into them, since one pass can hold several classes'
// draws (the tonemap and the UI). The frame is a fixed sequence of passes, so there's no render graph.
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

  void setDebugView(gpu::DebugView iDebugView) { mDebugView = iDebugView; }
  gpu::DebugView getDebugView() const { return mDebugView; }

private:
  static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

  void createTargets(VkExtent2D iExtent);
  void recordShadowPass(VkCommandBuffer iCommandBuffer, const ShadowCascades& iCascades);
  void recordScenePass(VkCommandBuffer iCommandBuffer, VkExtent2D iExtent, const SceneView* iCameraView);
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
  gpu::DebugView mDebugView{}; // Lit

  // Swapchain-sized; both frames in flight share them
  std::unique_ptr<Image> mSceneColor;
  std::unique_ptr<Image> mSceneDepth;
  uint32_t mSceneColorTextureIndex = 0; // Bindless slot, registered once and kept across resizes

  // One tile per cascade (ShadowCascades::getTile); both frames in flight share it
  std::unique_ptr<Image> mShadowAtlas;
  uint32_t mShadowAtlasTextureIndex = 0; // Bindless slot, registered once
};

} // namespace ne
