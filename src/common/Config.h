#pragma once

#include <filesystem>
#include <memory>
#include <functional>
#include <string>
#include <thread>

#include "GestureEngine.h"
#include "SettingsTypes.h"

namespace st {

struct AppConfig {
    GestureSettings gestures;

    // Which HID collections are accepted as input.
    DigitizerDevices devices = DigitizerDevices::Touchpad;

    // Which displays the brightness edge drives.
    BrightnessTargets brightnessTargets = BrightnessTargets::All;

    bool osdShowPercentage = true;
    bool startWithWindows = true;
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

// Writes the configuration in the current schema (same template the first-run
// file uses).  Used by the settings dialog.
bool SaveConfig(const std::filesystem::path& path, const AppConfig& config);

// Watches the configuration directory for writes to the INI file and invokes
// the callback from a worker thread.  The callback is debounced so an editor
// that performs several writes produces a single reload.
//
// Implemented per platform (ReadDirectoryChangesW, inotify, kqueue); the
// header only exposes the portable shape.
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

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace st
