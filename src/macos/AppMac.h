#pragma once

#include <memory>

#include "Config.h"

namespace st {

// macOS application shell: menu bar item, configuration reload, and the wiring
// from multitouch input to the shared gesture engine.
//
// This class is plain C++ so the rest of the program stays portable; the
// Objective-C objects live behind `Impl`.
class MacApp {
public:
    MacApp();
    ~MacApp();

    MacApp(const MacApp&) = delete;
    MacApp& operator=(const MacApp&) = delete;

    bool Initialize();
    // Runs until the user quits; returns the process exit code.
    int Run();

    void ReloadConfig();
    // `--zone-test`: log every contact with its resolved zone and value.
    void EnableContactLogging() noexcept { contactLogging_ = true; }

    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    void SetEnabled(bool enabled);

    void SetStartAtLogin(bool enabled);

    void OpenConfiguration();
    void OpenDataFolder();
    void OpenLog();

    [[nodiscard]] const AppConfig& Config() const noexcept { return config_; }

private:
    struct Impl;

    std::unique_ptr<Impl> impl_;
    AppConfig config_;
    bool enabled_ = true;
    bool contactLogging_ = false;
};

} // namespace st
