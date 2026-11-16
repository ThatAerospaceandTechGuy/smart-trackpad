#pragma once

// Window class names and the cross-process message used by the command-line
// switches.  A second instance forwards its request to the instance that
// already owns the trackpad; a top-level window is used as the target because
// message-only windows are not returned by FindWindow.
namespace st {

inline constexpr wchar_t kInputWindowClassName[] = L"SmartTrackpadInputWindow";
inline constexpr wchar_t kControlWindowClassName[] = L"SmartTrackpadTrayWindow";
inline constexpr wchar_t kCommandMessageName[] = L"SmartTrackpad.Command";

// Values sent in wParam of the command message.
inline constexpr unsigned kCommandOpenSettings = 1;
inline constexpr unsigned kCommandOpenZoneTest = 2;
inline constexpr unsigned kCommandExit = 3;

} // namespace st
