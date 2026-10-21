#pragma once

#include <memory>
#include <string>

namespace ne {

class Window;
class Instance;
class Device;
class FrameRenderer;
class ResourceManager;
class Renderer;
class ImGuiManager;
class Registry;

class Application {
public:
  Application(const std::string& iAppName = "Null Engine App", uint32_t iWidth = 1200, uint32_t iHeight = 1000);
  virtual ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  // Per-frame hooks: renderUI() builds the ImGui frame, then update() runs before transforms propagate and the frame renders
  virtual void update(float iDeltaTime);
  virtual void renderUI() {}

  void run();
  void stepFrame();
  void runForFrames(size_t iFrameCount = 1);

  Window* getWindow() const { return mWindow.get(); }
  ResourceManager* getResourceManager() const { return mResourceManager.get(); }
  Registry* getRegistry() const { return mRegistry.get(); }

protected:
  int32_t mWidth = 1200;
  int32_t mHeight = 1000;
  std::string mEngineName = "Null Engine";
  std::string mAppName = "Null App";

private:
  void render();

  std::unique_ptr<Instance> mInstance;
  std::unique_ptr<Device> mDevice;

  std::unique_ptr<Window> mWindow;
  std::unique_ptr<FrameRenderer> mFrameRenderer;

  std::unique_ptr<ResourceManager> mResourceManager;
  std::unique_ptr<Renderer> mRenderer;

  std::unique_ptr<ImGuiManager> mImGuiManager;
  std::unique_ptr<Registry> mRegistry;
};

} // namespace ne
