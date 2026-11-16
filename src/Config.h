#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <thread>

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

// Reads the INI file, creating it with documented defaults on first use and
// migrating a file written by an older release to the current schema.
// Out-of-range values are clamped; malformed values fall back to defaults.
[[nodiscard]] AppConfig LoadConfig(const std::filesystem::path& path);

// Watches the configuration directory for writes to the INI file and invokes
// the callback from a worker thread.  The callback is debounced so an editor
// that performs several writes produces a single reload.
class ConfigWatcher {
public:
    ConfigWatcher();
    ~ConfigWatcher();

    ConfigWatcher(const ConfigWatcher&) = delete;
    ConfigWatcher& operator=(const ConfigWatcher&) = delete;

    bool Watch(const std::filesystem::path& configPath, std::function<void()> onChanged);
    void Stop();

private:
    void Run(std::stop_token token);

    // Opaque Win32 handles so this header stays free of <windows.h>.
    void* directory_ = nullptr;
    void* stopEvent_ = nullptr;
    std::wstring fileName_;
    std::function<void()> onChanged_;
    std::jthread thread_;
};

} // namespace st
