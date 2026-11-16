#pragma once

#include <filesystem>

#include "GestureEngine.h"

namespace st {

struct AppConfig {
    GestureSettings gestures;
    bool debugLogging = false;
    std::filesystem::path configPath;
};

// %LOCALAPPDATA%\SmartTrackpad (or the executable directory as a fallback).
[[nodiscard]] std::filesystem::path GetDataDirectory();
[[nodiscard]] std::filesystem::path GetConfigPath();

// Reads the INI file, creating it with documented defaults on first use.
// Out-of-range values are clamped; malformed values fall back to defaults.
[[nodiscard]] AppConfig LoadConfig(const std::filesystem::path& path);

} // namespace st
