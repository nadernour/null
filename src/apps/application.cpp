#include "apps/application.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/logger.h"
#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"
#include "renderer/device.h"
#include "renderer/frame_renderer.h"
#include "renderer/imgui_manager.h"
#include "renderer/instance.h"
#include "renderer/renderer.h"
#include "renderer/resource_manager.h"
#include "renderer/utils.h"
#include "scene/transform_system.h"

namespace ne {

Application::Application(const std::string& iAppName, uint32_t iWidth, uint32_t iHeight)
    : mWidth(static_cast<int32_t>(iWidth)), mHeight(static_cast<int32_t>(iHeight)), mAppName(iAppName) {
  mWindow = std::make_unique<Window>(mWidth, mHeight, mAppName);

  Instance::Config instanceConfig{
      .engineName = mEngineName,
      .appName = mAppName,
      .requiredExtensions = mWindow->getRequiredInstanceExtensions(),
  };
  mInstance = std::make_unique<Instance>(instanceConfig);

  VK_CHECK(mWindow->createSurface(mInstance->getInstance()));

  Device::Config deviceConfig{
      .surface = mWindow->getSurface(),
  };
  mDevice = std::make_unique<Device>(mInstance.get(), deviceConfig);

  mFrameRenderer = std::make_unique<FrameRenderer>(mDevice.get(), mWindow.get());

  const VkFormat depthFormat = mDevice->findDepthFormat();
  mResourceManager = std::make_unique<ResourceManager>(mDevice.get(), Renderer::kSceneColorFormat, depthFormat);

  Input::init(mWindow.get());

  Renderer::Config rendererConfig{
      .device = mDevice.get(),
      .frameRenderer = mFrameRenderer.get(),
      .resourceManager = mResourceManager.get(),
      .depthFormat = depthFormat,
  };
  mRenderer = std::make_unique<Renderer>(rendererConfig);

  mImGuiManager = std::make_unique<ImGuiManager>(mWindow.get(), mInstance.get(), mDevice.get(), mFrameRenderer.get());

  mRegistry = std::make_unique<Registry>();

  NE_VERBOSE("Engine Application '{}' initialized successfully.", mAppName);
}

Application::~Application() {
  NE_VERBOSE("Tearing down Engine Application '{}'...", mAppName);

  // Reverse order of creation
  mRegistry.reset();
  mImGuiManager.reset();
  mRenderer.reset();
  mResourceManager.reset();
  mFrameRenderer.reset();
  mDevice.reset();

  mWindow->destroySurface(mInstance->getInstance());

  mInstance.reset();
  mWindow.reset();

  NE_VERBOSE("Engine Application '{}' destroyed successfully.", mAppName);
}

void Application::update(float) {}

void Application::render() { mRenderer->render(mRegistry.get(), mImGuiManager.get()); }

void Application::stepFrame() {
  Time::tick();
  Input::update();
  mWindow->processEvents();

  mImGuiManager->beginFrame();
  Input::setUICapture(mImGuiManager->wantsCaptureMouse(), mImGuiManager->wantsCaptureKeyboard());
  renderUI();
  mImGuiManager->endFrame();

  update(Time::getDeltaTime());
  TransformSystem::update(*mRegistry);
  render();
}

void Application::runForFrames(size_t iFrameCount) {
  for (size_t i = 0; i < iFrameCount && !mWindow->shouldClose(); ++i) {
    stepFrame();
  }
  mDevice->waitIdle();
}

void Application::run() {
  NE_LOG("{} Start!", mAppName);

  // Started here so the first frame's delta does not include device creation and asset loading
  Time::init();
  while (!mWindow->shouldClose()) {
    stepFrame();
  }

  mDevice->waitIdle();
  NE_LOG("{} Done!", mAppName);
}

} // namespace ne
