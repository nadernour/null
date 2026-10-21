#ifndef NE_BUILD_SHIPPING

#include "tests/test_runner.h"
#include "apps/application.h"
#include "platform/window.h"
#include "renderer/bindless_manager.h"
#include "renderer/resource_manager.h"

#include <GLFW/glfw3.h>

namespace ne::test {

// In CI this runs the whole frame on lavapipe under synchronization validation; Debug builds abort at exit on any error
NE_TEST_CASE("renderer", "Frames Render Through Scene, Tonemap, And UI") {
  ioContext.app->runForFrames(3);
  NE_TEST_ASSERT(!ioContext.app->getWindow()->shouldClose(), "The window stayed open, so all three frames ran.");
}

// A tiling window manager may ignore the resize; under CI's Xvfb it happens
NE_TEST_CASE("renderer", "A Resize Keeps The HDR Target's Bindless Slot") {
  const BindlessManager& bindless = *ioContext.app->getResourceManager()->getBindlessManager();
  const uint32_t registeredBefore = bindless.getRegisteredSampledImageCount();
  Window& window = *ioContext.app->getWindow();
  int32_t width = 0, height = 0;
  window.getWindowSize(&width, &height);

  glfwSetWindowSize(window.getGLFWwindow(), 640, 480);
  ioContext.app->runForFrames(3);
  glfwSetWindowSize(window.getGLFWwindow(), width, height);
  ioContext.app->runForFrames(3);

  NE_TEST_ASSERT(bindless.getRegisteredSampledImageCount() == registeredBefore,
                 "Resizing reuses the HDR target's bindless slot instead of taking a new one.");
}

} // namespace ne::test

#endif
