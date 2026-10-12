#include "platform/input.h"
#include "core/logger.h"
#include "platform/window.h"

#include <GLFW/glfw3.h>

namespace ne {

Input::State Input::sState{};

void Input::init(Window* iWindow) {
  if (sState.mWindow && sState.mFocusCallbackId != 0) {
    sState.mWindow->removeWindowFocusCallback(sState.mFocusCallbackId);
    sState.mFocusCallbackId = 0;
  }

  sState.mWindow = iWindow;
  if (sState.mWindow) {
    GLFWwindow* glfwWin = sState.mWindow->getGLFWwindow();
    glfwSetKeyCallback(glfwWin, keyCallback);
    glfwSetMouseButtonCallback(glfwWin, mouseButtonCallback);
    glfwSetCursorPosCallback(glfwWin, cursorPosCallback);
    glfwSetScrollCallback(glfwWin, scrollCallback);

    sState.mFocusCallbackId = sState.mWindow->addWindowFocusCallback([](bool iFocused) {
      if (!iFocused) {
        resetState();
      }
    });

    sState.mWindow->getCursorPos(&sState.mLastMouseX, &sState.mLastMouseY);
    sState.mMouseX = sState.mLastMouseX;
    sState.mMouseY = sState.mLastMouseY;
  }

  sState.mInitialized = true;
  sState.mFirstMouse = true;
  sState.mFirstMouseAfterCapture = false;
  resetState();

  NE_LOG("Input subsystem initialized successfully.");
}

void Input::update() {
  sState.mJustPressedKeys.reset();
  sState.mJustReleasedKeys.reset();

  sState.mJustPressedMouse.reset();
  sState.mJustReleasedMouse.reset();

  sState.mMouseDelta = Vec2(0.0f, 0.0f);
  sState.mMouseScroll = Vec2(0.0f, 0.0f);
}

bool Input::isKeyDown(KeyCode iKey) {
  if (sState.mUIKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.mCurrentKeys.test(static_cast<size_t>(idx));
}

bool Input::isKeyPressed(KeyCode iKey) {
  if (sState.mUIKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.mJustPressedKeys.test(static_cast<size_t>(idx));
}

bool Input::isKeyReleased(KeyCode iKey) {
  if (sState.mUIKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.mJustReleasedKeys.test(static_cast<size_t>(idx));
}

KeyMods Input::getActiveMods() { return sState.mActiveMods; }

bool Input::isShiftDown() { return isKeyDown(KeyCode::LeftShift) || isKeyDown(KeyCode::RightShift); }

bool Input::isControlDown() { return isKeyDown(KeyCode::LeftControl) || isKeyDown(KeyCode::RightControl); }

bool Input::isAltDown() { return isKeyDown(KeyCode::LeftAlt) || isKeyDown(KeyCode::RightAlt); }

bool Input::isSuperDown() { return isKeyDown(KeyCode::LeftSuper) || isKeyDown(KeyCode::RightSuper); }

bool Input::isMouseButtonDown(MouseButton iButton) {
  if (sState.mUIMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.mCurrentMouse.test(idx);
}

bool Input::isMouseButtonPressed(MouseButton iButton) {
  if (sState.mUIMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.mJustPressedMouse.test(idx);
}

bool Input::isMouseButtonReleased(MouseButton iButton) {
  if (sState.mUIMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.mJustReleasedMouse.test(idx);
}

Vec2 Input::getMousePosition() { return Vec2(static_cast<float>(sState.mMouseX), static_cast<float>(sState.mMouseY)); }

Vec2 Input::getMouseDelta() {
  if (sState.mUIMouseCaptured) {
    return Vec2(0.0f, 0.0f);
  }
  return sState.mMouseDelta;
}

Vec2 Input::getMouseScroll() {
  if (sState.mUIMouseCaptured) {
    return Vec2(0.0f, 0.0f);
  }
  return sState.mMouseScroll;
}

void Input::setCursorMode(CursorMode iMode) {
  if (sState.mCursorMode != iMode) {
    sState.mCursorMode = iMode;
    if (iMode == CursorMode::Disabled) {
      sState.mFirstMouseAfterCapture = true;
    }
    if (sState.mWindow) {
      sState.mWindow->setCursorMode(iMode);
    }
  }
}

CursorMode Input::getCursorMode() { return sState.mCursorMode; }

void Input::setUICapture(bool iCaptureMouse, bool iCaptureKeyboard) {
  sState.mUIMouseCaptured = iCaptureMouse;
  sState.mUIKeyboardCaptured = iCaptureKeyboard;
}

bool Input::isMouseCapturedByUI() { return sState.mUIMouseCaptured; }

bool Input::isKeyboardCapturedByUI() { return sState.mUIKeyboardCaptured; }

void Input::resetState() {
  sState.mCurrentKeys.reset();
  sState.mJustPressedKeys.reset();
  sState.mJustReleasedKeys.reset();

  sState.mCurrentMouse.reset();
  sState.mJustPressedMouse.reset();
  sState.mJustReleasedMouse.reset();

  sState.mActiveMods = KeyMods::None;
  sState.mMouseDelta = Vec2(0.0f, 0.0f);
  sState.mMouseScroll = Vec2(0.0f, 0.0f);
  sState.mUIMouseCaptured = false;
  sState.mUIKeyboardCaptured = false;

  if (sState.mCursorMode == CursorMode::Disabled) {
    setCursorMode(CursorMode::Normal);
  }
}

void Input::keyCallback(GLFWwindow*, int iKey, int, int iAction, int iMods) {
  int16_t keyIdx = static_cast<int16_t>(iKey);
  if (keyIdx >= 0 && static_cast<size_t>(keyIdx) < kMaxKeys) {
    size_t idx = static_cast<size_t>(keyIdx);
    if (iAction == GLFW_PRESS) {
      sState.mCurrentKeys.set(idx, true);
      sState.mJustPressedKeys.set(idx, true);
    } else if (iAction == GLFW_RELEASE) {
      sState.mCurrentKeys.set(idx, false);
      sState.mJustReleasedKeys.set(idx, true);
    }
  }
  sState.mActiveMods = static_cast<KeyMods>(iMods);
}

void Input::mouseButtonCallback(GLFWwindow*, int iButton, int iAction, int iMods) {
  size_t btnIdx = static_cast<size_t>(iButton);
  if (btnIdx < kMaxMouseButtons) {
    if (iAction == GLFW_PRESS) {
      sState.mCurrentMouse.set(btnIdx, true);
      sState.mJustPressedMouse.set(btnIdx, true);
    } else if (iAction == GLFW_RELEASE) {
      sState.mCurrentMouse.set(btnIdx, false);
      sState.mJustReleasedMouse.set(btnIdx, true);
    }
  }
  sState.mActiveMods = static_cast<KeyMods>(iMods);
}

void Input::cursorPosCallback(GLFWwindow*, double iXpos, double iYpos) {
  if (sState.mFirstMouse || sState.mFirstMouseAfterCapture) {
    sState.mLastMouseX = iXpos;
    sState.mLastMouseY = iYpos;
    sState.mMouseX = iXpos;
    sState.mMouseY = iYpos;
    sState.mFirstMouse = false;
    sState.mFirstMouseAfterCapture = false;
    return;
  }

  double dx = iXpos - sState.mLastMouseX;
  double dy = iYpos - sState.mLastMouseY;
  sState.mMouseDelta.x += static_cast<float>(dx);
  sState.mMouseDelta.y += static_cast<float>(dy);

  sState.mLastMouseX = iXpos;
  sState.mLastMouseY = iYpos;
  sState.mMouseX = iXpos;
  sState.mMouseY = iYpos;
}

void Input::scrollCallback(GLFWwindow*, double iXoffset, double iYoffset) {
  sState.mMouseScroll.x += static_cast<float>(iXoffset);
  sState.mMouseScroll.y += static_cast<float>(iYoffset);
}

} // namespace ne
