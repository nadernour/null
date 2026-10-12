#pragma once

#include <cstdint>

namespace ne {

enum class KeyCode : int16_t {
  Unknown = -1,

  // Printable keys
  Space = 32,
  Apostrophe = 39, Comma = 44, Minus = 45, Period = 46, Slash = 47,
  Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52,
  Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57,
  Semicolon = 59, Equal = 61,
  A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71, H = 72, I = 73,
  J = 74, K = 75, L = 76, M = 77, N = 78, O = 79, P = 80, Q = 81, R = 82,
  S = 83, T = 84, U = 85, V = 86, W = 87, X = 88, Y = 89, Z = 90,
  LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,

  // Function & Control keys
  Escape = 256, Enter = 257, Tab = 258, Backspace = 259, Insert = 260, Delete = 261,
  Right = 262, Left = 263, Down = 264, Up = 265, PageUp = 266, PageDown = 267,
  Home = 268, End = 269, CapsLock = 280, ScrollLock = 281, NumLock = 282,
  PrintScreen = 283, Pause = 284,
  F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295,
  F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301,
  LeftShift = 340, LeftControl = 341, LeftAlt = 342, LeftSuper = 343,
  RightShift = 344, RightControl = 345, RightAlt = 346, RightSuper = 347,
  Menu = 348
};

enum class MouseButton : uint8_t {
  Button0 = 0, Button1 = 1, Button2 = 2, Button3 = 3,
  Button4 = 4, Button5 = 5, Button6 = 6, Button7 = 7,
  Left = Button0, Right = Button1, Middle = Button2
};

enum class InputAction : uint8_t {
  Release = 0,
  Press = 1,
  Repeat = 2
};

enum class KeyMods : uint8_t {
  None = 0,
  Shift = 1 << 0,
  Control = 1 << 1,
  Alt = 1 << 2,
  Super = 1 << 3,
  CapsLock = 1 << 4,
  NumLock = 1 << 5
};

inline KeyMods operator|(KeyMods iA, KeyMods iB) {
  return static_cast<KeyMods>(static_cast<uint8_t>(iA) | static_cast<uint8_t>(iB));
}

inline bool operator&(KeyMods iA, KeyMods iB) {
  return (static_cast<uint8_t>(iA) & static_cast<uint8_t>(iB)) != 0;
}

enum class CursorMode : uint8_t {
  Normal = 0,
  Hidden = 1,
  Disabled = 2
};

} // namespace ne
