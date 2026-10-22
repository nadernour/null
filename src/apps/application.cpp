#include "apps/application.h"
#include "core/assert.h"
#include "core/ecs.h"
#include "core/logger.h"
#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"
#include "renderer/imgui_manager.h"
#include "renderer/renderer.h"
#include "scene/transform_system.h"

namespace ne {

Application::Application(const std::string& iAppName, uint32_t iWidth, uint32_t iHeight)
    : mWidth(static_cast<int32_t>(iWidth)), mHeight(static_cast<int32_t>(iHeight)), mAppName(iAppName) {
  mWindow = std::make_unique<Window>(mWidth, mHeight, mAppName);

  // Before the renderer: ImGui's GLFW backend chains to the input callbacks installed here
  Input::init(mWindow.get());

  mRenderer = std::make_unique<Renderer>(mWindow.get(), mAppName);

  mRegistry = std::make_unique<Registry>();

  NE_VERBOSE("Engine Application '{}' initialized successfully.", mAppName);
}

Application::~Application() {
  NE_VERBOSE("Tearing down Engine Application '{}'...", mAppName);

  // Reverse order of creation
  mRegistry.reset();
  mRenderer.reset();
  mWindow.reset();

  NE_VERBOSE("Engine Application '{}' destroyed successfully.", mAppName);
}

ResourceManager* Application::getResourceManager() const { return mRenderer->getResourceManager(); }

void Application::update(float) {}

void Application::render() { mRenderer->render(*mRegistry); }

void Application::stepFrame() {
  Time::tick();
  Input::update();
  mWindow->processEvents();

  ImGuiManager& imGuiManager = *mRenderer->getImGuiManager();
  imGuiManager.beginFrame();
  Input::setUICapture(imGuiManager.wantsCaptureMouse(), imGuiManager.wantsCaptureKeyboard());
  renderUI();
  imGuiManager.endFrame();

  update(Time::getDeltaTime());
  TransformSystem::update(*mRegistry);
  render();
}

void Application::runForFrames(size_t iFrameCount) {
  for (size_t i = 0; i < iFrameCount && !mWindow->shouldClose(); ++i) {
    stepFrame();
  }
  mRenderer->waitIdle();
}

void Application::run() {
  NE_LOG("{} Start!", mAppName);

  // Started here so the first frame's delta does not include device creation and asset loading
  Time::init();
  while (!mWindow->shouldClose()) {
    stepFrame();
  }

  mRenderer->waitIdle();
  NE_LOG("{} Done!", mAppName);
}

} // namespace ne
