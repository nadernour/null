#pragma once

#include "core/event.h"
#include "platform/input_types.h"
#include <volk/volk.h>

// std
#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWmonitor;

namespace ne {

class Window {
public:
  using FrameBufferResizeEvent = Event<int32_t, int32_t>;
  using WindowFocusEvent = Event<bool>;

  Window(int32_t iWidth, int32_t iHeight, const std::string& iName);
  ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;

  bool shouldClose() const;
  void setShouldClose(bool iClose = true);
  void processEvents();
  void waitEvents();
  void getFrameBufferSize(int32_t* oWidth, int32_t* oHeight) const;
  void getWindowSize(int32_t* oWidth, int32_t* oHeight) const;
  GLFWwindow* getGLFWwindow() const { return mWindow; }
  GLFWmonitor* getPrimaryMonitor() const;
  const char* getWindowName() const;
  void setTitle(const std::string& iTitle);
  std::vector<const char*> getRequiredInstanceExtensions() const;
  VkResult createSurface(VkInstance iInstance);
  void destroySurface(VkInstance iInstance);
  VkSurfaceKHR getSurface() const { return mSurface; }

  void getCursorPos(double* oXpos, double* oYpos) const;
  void setCursorMode(CursorMode iMode);

  CallbackId addFrameBufferResizeCallback(FrameBufferResizeEvent::Callback iCallback) {
    return mFrameBufferResizeEvent.add(std::move(iCallback));
  }
  CallbackId addWindowFocusCallback(WindowFocusEvent::Callback iCallback) { return mWindowFocusEvent.add(std::move(iCallback)); }

  void removeFrameBufferResizeCallback(CallbackId iId) { mFrameBufferResizeEvent.remove(iId); }
  void removeWindowFocusCallback(CallbackId iId) { mWindowFocusEvent.remove(iId); }

private:
  static void framebufferResizeCallback(GLFWwindow* iWindow, int iWidth, int iHeight);
  static void windowFocusCallback(GLFWwindow* iWindow, int iFocused);

  GLFWwindow* mWindow;
  VkSurfaceKHR mSurface = VK_NULL_HANDLE;

  FrameBufferResizeEvent mFrameBufferResizeEvent;
  WindowFocusEvent mWindowFocusEvent;
};

} // namespace ne
