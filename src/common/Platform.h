#pragma once

#include <filesystem>

namespace st {

// Absolute path of the running executable (empty if it cannot be determined).
[[nodiscard]] std::filesystem::path ExecutablePath();
[[nodiscard]] std::filesystem::path ExecutableDirectory();

// Per-user data directory:
//   Windows  %LOCALAPPDATA%\SmartTrackpad
//   macOS    ~/Library/Application Support/SmartTrackpad
//   Linux    $XDG_DATA_HOME/SmartTrackpad (default ~/.local/share/SmartTrackpad)
// Falls back to a directory next to the executable when the platform location
// is unavailable.
[[nodiscard]] std::filesystem::path GetDataDirectory();

// Per-user configuration file inside the data directory.
[[nodiscard]] std::filesystem::path GetConfigPath();

// True when the current session provides the platform's input/audio plumbing at
// all; used only to produce a helpful log line on servers without a session.
[[nodiscard]] bool HasInteractiveSession();

} // namespace st
