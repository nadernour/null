#include "platform/input.h"
#include "core/logger.h"
#include "platform/window.h"

#include <GLFW/glfw3.h>

namespace ne {

Input::State Input::sState{};

void Input::init(Window* iWindow) {
  if (sState.window && sState.focusCallbackId != 0) {
    sState.window->removeWindowFocusCallback(sState.focusCallbackId);
    sState.focusCallbackId = 0;
  }

  sState.window = iWindow;
  if (sState.window) {
    GLFWwindow* glfwWin = sState.window->getGLFWwindow();
    glfwSetKeyCallback(glfwWin, keyCallback);
    glfwSetMouseButtonCallback(glfwWin, mouseButtonCallback);
    glfwSetCursorPosCallback(glfwWin, cursorPosCallback);
    glfwSetScrollCallback(glfwWin, scrollCallback);

    sState.focusCallbackId = sState.window->addWindowFocusCallback([](bool iFocused) {
      if (!iFocused) {
        resetState();
      }
    });

    sState.window->getCursorPos(&sState.lastMouseX, &sState.lastMouseY);
    sState.mouseX = sState.lastMouseX;
    sState.mouseY = sState.lastMouseY;
  }

  sState.initialized = true;
  sState.firstMouse = true;
  sState.firstMouseAfterCapture = false;
  resetState();

  NE_VERBOSE("Input subsystem initialized successfully.");
}

void Input::update() {
  sState.justPressedKeys.reset();
  sState.justReleasedKeys.reset();

  sState.justPressedMouse.reset();
  sState.justReleasedMouse.reset();

  sState.mouseDelta = Vec2(0.0f, 0.0f);
  sState.mouseScroll = Vec2(0.0f, 0.0f);
}

bool Input::isKeyDown(KeyCode iKey) {
  if (sState.uiKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.currentKeys.test(static_cast<size_t>(idx));
}

bool Input::isKeyPressed(KeyCode iKey) {
  if (sState.uiKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.justPressedKeys.test(static_cast<size_t>(idx));
}

bool Input::isKeyReleased(KeyCode iKey) {
  if (sState.uiKeyboardCaptured) {
    return false;
  }
  int16_t idx = static_cast<int16_t>(iKey);
  return idx >= 0 && static_cast<size_t>(idx) < kMaxKeys && sState.justReleasedKeys.test(static_cast<size_t>(idx));
}

KeyMods Input::getActiveMods() { return sState.activeMods; }

bool Input::isShiftDown() { return isKeyDown(KeyCode::LeftShift) || isKeyDown(KeyCode::RightShift); }

bool Input::isControlDown() { return isKeyDown(KeyCode::LeftControl) || isKeyDown(KeyCode::RightControl); }

bool Input::isAltDown() { return isKeyDown(KeyCode::LeftAlt) || isKeyDown(KeyCode::RightAlt); }

bool Input::isSuperDown() { return isKeyDown(KeyCode::LeftSuper) || isKeyDown(KeyCode::RightSuper); }

bool Input::isMouseButtonDown(MouseButton iButton) {
  if (sState.uiMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.currentMouse.test(idx);
}

bool Input::isMouseButtonPressed(MouseButton iButton) {
  if (sState.uiMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.justPressedMouse.test(idx);
}

bool Input::isMouseButtonReleased(MouseButton iButton) {
  if (sState.uiMouseCaptured) {
    return false;
  }
  size_t idx = static_cast<size_t>(iButton);
  return idx < kMaxMouseButtons && sState.justReleasedMouse.test(idx);
}

Vec2 Input::getMousePosition() { return Vec2(static_cast<float>(sState.mouseX), static_cast<float>(sState.mouseY)); }

Vec2 Input::getMouseDelta() {
  if (sState.uiMouseCaptured) {
    return Vec2(0.0f, 0.0f);
  }
  return sState.mouseDelta;
}

Vec2 Input::getMouseScroll() {
  if (sState.uiMouseCaptured) {
    return Vec2(0.0f, 0.0f);
  }
  return sState.mouseScroll;
}

void Input::setCursorMode(CursorMode iMode) {
  if (sState.cursorMode != iMode) {
    sState.cursorMode = iMode;
    if (iMode == CursorMode::Disabled) {
      sState.firstMouseAfterCapture = true;
    }
    if (sState.window) {
      sState.window->setCursorMode(iMode);
    }
  }
}

CursorMode Input::getCursorMode() { return sState.cursorMode; }

void Input::setUICapture(bool iCaptureMouse, bool iCaptureKeyboard) {
  sState.uiMouseCaptured = iCaptureMouse;
  sState.uiKeyboardCaptured = iCaptureKeyboard;
}

bool Input::isMouseCapturedByUI() { return sState.uiMouseCaptured; }

bool Input::isKeyboardCapturedByUI() { return sState.uiKeyboardCaptured; }

void Input::resetState() {
  sState.currentKeys.reset();
  sState.justPressedKeys.reset();
  sState.justReleasedKeys.reset();

  sState.currentMouse.reset();
  sState.justPressedMouse.reset();
  sState.justReleasedMouse.reset();

  sState.activeMods = KeyMods::None;
  sState.mouseDelta = Vec2(0.0f, 0.0f);
  sState.mouseScroll = Vec2(0.0f, 0.0f);
  sState.uiMouseCaptured = false;
  sState.uiKeyboardCaptured = false;

  if (sState.cursorMode == CursorMode::Disabled) {
    setCursorMode(CursorMode::Normal);
  }
}

void Input::keyCallback(GLFWwindow*, int iKey, int, int iAction, int iMods) {
  int16_t keyIdx = static_cast<int16_t>(iKey);
  if (keyIdx >= 0 && static_cast<size_t>(keyIdx) < kMaxKeys) {
    size_t idx = static_cast<size_t>(keyIdx);
    if (iAction == GLFW_PRESS) {
      sState.currentKeys.set(idx, true);
      sState.justPressedKeys.set(idx, true);
    } else if (iAction == GLFW_RELEASE) {
      sState.currentKeys.set(idx, false);
      sState.justReleasedKeys.set(idx, true);
    }
  }
  sState.activeMods = static_cast<KeyMods>(iMods);
}

void Input::mouseButtonCallback(GLFWwindow*, int iButton, int iAction, int iMods) {
  size_t btnIdx = static_cast<size_t>(iButton);
  if (btnIdx < kMaxMouseButtons) {
    if (iAction == GLFW_PRESS) {
      sState.currentMouse.set(btnIdx, true);
      sState.justPressedMouse.set(btnIdx, true);
    } else if (iAction == GLFW_RELEASE) {
      sState.currentMouse.set(btnIdx, false);
      sState.justReleasedMouse.set(btnIdx, true);
    }
  }
  sState.activeMods = static_cast<KeyMods>(iMods);
}

void Input::cursorPosCallback(GLFWwindow*, double iXpos, double iYpos) {
  if (sState.firstMouse || sState.firstMouseAfterCapture) {
    sState.lastMouseX = iXpos;
    sState.lastMouseY = iYpos;
    sState.mouseX = iXpos;
    sState.mouseY = iYpos;
    sState.firstMouse = false;
    sState.firstMouseAfterCapture = false;
    return;
  }

  double dx = iXpos - sState.lastMouseX;
  double dy = iYpos - sState.lastMouseY;
  sState.mouseDelta.x += static_cast<float>(dx);
  sState.mouseDelta.y += static_cast<float>(dy);

  sState.lastMouseX = iXpos;
  sState.lastMouseY = iYpos;
  sState.mouseX = iXpos;
  sState.mouseY = iYpos;
}

void Input::scrollCallback(GLFWwindow*, double iXoffset, double iYoffset) {
  sState.mouseScroll.x += static_cast<float>(iXoffset);
  sState.mouseScroll.y += static_cast<float>(iYoffset);
}

} // namespace ne
