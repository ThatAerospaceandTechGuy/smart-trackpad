#pragma once

#include <memory>

#include "Config.h"

namespace st {

// Wires the shared gesture engine to the platform backends on Linux and runs
// the event loop.  There is no tray icon (that would need GTK or an SNI
// implementation); the configuration file and the command line are the
// interface, plus optional autostart.
class LinuxApp {
public:
    LinuxApp();
    ~LinuxApp();

    LinuxApp(const LinuxApp&) = delete;
    LinuxApp& operator=(const LinuxApp&) = delete;

    bool Initialize();
    int Run();

    // Applies a new configuration without restarting.
    void ReloadConfig();
    // Writes the INI and starts/removes the autostart entry.
    void ApplyStartWithWindows(bool enabled);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace st
