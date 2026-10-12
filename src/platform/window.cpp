#include "platform/window.h"
#include "core/logger.h"

#include <GLFW/glfw3.h>

namespace ne {

Window::Window(int32_t iWidth, int32_t iHeight, const std::string& iName) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

  mWindow = glfwCreateWindow(iWidth, iHeight, iName.c_str(), nullptr, nullptr);
  NE_VERBOSE("Initialized GLFW and created window '{}' ({}x{})", iName, iWidth, iHeight);

  glfwSetWindowUserPointer(mWindow, this);
  glfwSetFramebufferSizeCallback(mWindow, framebufferResizeCallback);
  glfwSetWindowFocusCallback(mWindow, windowFocusCallback);
}

Window::~Window() {
  glfwDestroyWindow(mWindow);
  glfwTerminate();
  NE_VERBOSE("Destroyed window and terminated GLFW");
}

void Window::processEvents() { glfwPollEvents(); }

void Window::waitEvents() { glfwWaitEvents(); }

const char* Window::getWindowName() const { return glfwGetWindowTitle(mWindow); }

void Window::setTitle(const std::string& iTitle) { glfwSetWindowTitle(mWindow, iTitle.c_str()); }

bool Window::shouldClose() const { return glfwWindowShouldClose(mWindow); }

void Window::setShouldClose(bool iClose) { glfwSetWindowShouldClose(mWindow, iClose ? GLFW_TRUE : GLFW_FALSE); }

void Window::getFrameBufferSize(int32_t* oWidth, int32_t* oHeight) const { glfwGetFramebufferSize(mWindow, oWidth, oHeight); }

void Window::getWindowSize(int32_t* oWidth, int32_t* oHeight) const { glfwGetWindowSize(mWindow, oWidth, oHeight); }

GLFWmonitor* Window::getPrimaryMonitor() const { return glfwGetPrimaryMonitor(); }

std::vector<const char*> Window::getRequiredInstanceExtensions() const {
  uint32_t count = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&count);
  std::vector<const char*> result(extensions, extensions + count);
  return result;
}

VkResult Window::createSurface(VkInstance iInstance) { return glfwCreateWindowSurface(iInstance, mWindow, nullptr, &mSurface); }

void Window::destroySurface(VkInstance iInstance) {
  if (mSurface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(iInstance, mSurface, nullptr);
    mSurface = VK_NULL_HANDLE;
  }
}

void Window::getCursorPos(double* oXpos, double* oYpos) const { glfwGetCursorPos(mWindow, oXpos, oYpos); }

void Window::setCursorMode(CursorMode iMode) {
  int glfwMode = GLFW_CURSOR_NORMAL;
  switch (iMode) {
    case CursorMode::Normal:
      glfwMode = GLFW_CURSOR_NORMAL;
      break;
    case CursorMode::Hidden:
      glfwMode = GLFW_CURSOR_HIDDEN;
      break;
    case CursorMode::Disabled:
      glfwMode = GLFW_CURSOR_DISABLED;
      break;
  }
  glfwSetInputMode(mWindow, GLFW_CURSOR, glfwMode);
  if (iMode == CursorMode::Disabled) {
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
  } else {
    if (glfwRawMouseMotionSupported()) {
      glfwSetInputMode(mWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
  }
}

void Window::framebufferResizeCallback(GLFWwindow* iGLFWindow, int iWidth, int iHeight) {
  Window* window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(iGLFWindow));
  if (window) {
    window->mFrameBufferResizeEvent.broadcast(static_cast<int32_t>(iWidth), static_cast<int32_t>(iHeight));
  }
}

void Window::windowFocusCallback(GLFWwindow* iGLFWindow, int iFocused) {
  Window* window = reinterpret_cast<Window*>(glfwGetWindowUserPointer(iGLFWindow));
  if (window) {
    window->mWindowFocusEvent.broadcast(iFocused != 0);
  }
}

} // namespace ne
