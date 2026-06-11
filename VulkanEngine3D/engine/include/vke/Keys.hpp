#pragma once

// GLFW key/mouse-button codes for use in scripts (Script::keyDown /
// mouseButtonDown) without pulling in GLFW headers. Values mirror GLFW's
// public API and are stable.

namespace vke::Key {

inline constexpr int Space        = 32;
inline constexpr int Apostrophe   = 39;
inline constexpr int Comma        = 44;
inline constexpr int Minus        = 45;
inline constexpr int Period       = 46;
inline constexpr int Slash        = 47;

inline constexpr int Num0 = 48, Num1 = 49, Num2 = 50, Num3 = 51, Num4 = 52;
inline constexpr int Num5 = 53, Num6 = 54, Num7 = 55, Num8 = 56, Num9 = 57;

inline constexpr int A = 65, B = 66, C = 67, D = 68, E = 69, F = 70, G = 71;
inline constexpr int H = 72, I = 73, J = 74, K = 75, L = 76, M = 77, N = 78;
inline constexpr int O = 79, P = 80, Q = 81, R = 82, S = 83, T = 84, U = 85;
inline constexpr int V = 86, W = 87, X = 88, Y = 89, Z = 90;

inline constexpr int Escape       = 256;
inline constexpr int Enter        = 257;
inline constexpr int Tab          = 258;
inline constexpr int Backspace    = 259;
inline constexpr int Insert       = 260;
inline constexpr int Delete       = 261;
inline constexpr int Right        = 262;
inline constexpr int Left         = 263;
inline constexpr int Down         = 264;
inline constexpr int Up           = 265;
inline constexpr int PageUp       = 266;
inline constexpr int PageDown     = 267;
inline constexpr int Home         = 268;
inline constexpr int End          = 269;

inline constexpr int F1 = 290, F2 = 291, F3 = 292, F4 = 293, F5 = 294, F6 = 295;
inline constexpr int F7 = 296, F8 = 297, F9 = 298, F10 = 299, F11 = 300, F12 = 301;

inline constexpr int LeftShift    = 340;
inline constexpr int LeftControl  = 341;
inline constexpr int LeftAlt      = 342;
inline constexpr int RightShift   = 344;
inline constexpr int RightControl = 345;
inline constexpr int RightAlt     = 346;

} // namespace vke::Key

namespace vke::Mouse {

inline constexpr int Left   = 0;
inline constexpr int Right  = 1;
inline constexpr int Middle = 2;

} // namespace vke::Mouse
