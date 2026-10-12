#pragma once

#include "core/event.h"
#include "core/math/math.h"
#include "platform/input_types.h"

#include <bitset>
#include <cstdint>

struct GLFWwindow;

namespace ne {

class Window;

class Input {
public:
  static void init(Window* iWindow);
  static void update();

  // Key and mouse queries report nothing while the UI captures that device
  static bool isKeyDown(KeyCode iKey);
  static bool isKeyPressed(KeyCode iKey);
  static bool isKeyReleased(KeyCode iKey);
  static KeyMods getActiveMods();

  static bool isShiftDown();
  static bool isControlDown();
  static bool isAltDown();
  static bool isSuperDown();

  static bool isMouseButtonDown(MouseButton iButton);
  static bool isMouseButtonPressed(MouseButton iButton);
  static bool isMouseButtonReleased(MouseButton iButton);

  static Vec2 getMousePosition();
  static Vec2 getMouseDelta();
  static Vec2 getMouseScroll();

  static void setCursorMode(CursorMode iMode);
  static CursorMode getCursorMode();

  static void setUICapture(bool iCaptureMouse, bool iCaptureKeyboard);
  static bool isMouseCapturedByUI();
  static bool isKeyboardCapturedByUI();

  static void resetState();

private:
  static constexpr size_t kMaxKeys = 512;
  static constexpr size_t kMaxMouseButtons = 16;

  static void keyCallback(GLFWwindow* iWindow, int iKey, int iScancode, int iAction, int iMods);
  static void mouseButtonCallback(GLFWwindow* iWindow, int iButton, int iAction, int iMods);
  static void cursorPosCallback(GLFWwindow* iWindow, double iXpos, double iYpos);
  static void scrollCallback(GLFWwindow* iWindow, double iXoffset, double iYoffset);

  struct State {
    Window* window{nullptr};
    CallbackId focusCallbackId{0};
    CursorMode cursorMode{CursorMode::Normal};

    std::bitset<kMaxKeys> currentKeys;
    std::bitset<kMaxKeys> justPressedKeys;
    std::bitset<kMaxKeys> justReleasedKeys;

    std::bitset<kMaxMouseButtons> currentMouse;
    std::bitset<kMaxMouseButtons> justPressedMouse;
    std::bitset<kMaxMouseButtons> justReleasedMouse;

    KeyMods activeMods{KeyMods::None};

    double mouseX{0.0};
    double mouseY{0.0};
    double lastMouseX{0.0};
    double lastMouseY{0.0};
    Vec2 mouseDelta{0.0f, 0.0f};
    Vec2 mouseScroll{0.0f, 0.0f};

    bool uiMouseCaptured{false};
    bool uiKeyboardCaptured{false};

    bool firstMouse{true};
    bool firstMouseAfterCapture{false};
    bool initialized{false};
  };

  static State sState;
};

} // namespace ne
