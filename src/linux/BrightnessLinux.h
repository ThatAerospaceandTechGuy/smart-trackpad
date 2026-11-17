#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Feedback.h"
#include "SettingsTypes.h"

namespace st {

// One backlight device found in /sys/class/backlight.
struct BacklightDevice {
    std::filesystem::path directory;
    std::string name;
    int maximum = 0;
    bool writable = false;
};

// System brightness.
//
// Primary backend is /sys/class/backlight (the kernel exposes every panel
// there).  Writing needs permission; when the sysfs node is not writable the
// controller falls back to `brightnessctl`, which is setuid on most
// distributions, and logs which backend actually worked.
//
// DDC/CI monitors are not driven: that needs the i2c device nodes and a
// dedicated protocol implementation, which is documented as out of scope.
class LinuxBrightnessController {
public:
    explicit LinuxBrightnessController(LevelFeedbackFn feedback);
    ~LinuxBrightnessController();

    LinuxBrightnessController(const LinuxBrightnessController&) = delete;
    LinuxBrightnessController& operator=(const LinuxBrightnessController&) = delete;

    void SetTargets(BrightnessTargets targets);
    void SetTargetPercent(int percent);

    [[nodiscard]] bool Available() const noexcept { return available_; }
    [[nodiscard]] const std::vector<BacklightDevice>& Devices() const noexcept {
        return devices_;
    }
    [[nodiscard]] const char* BackendName() const noexcept;

private:
    void Apply(int percent);

    LevelFeedbackFn feedback_;
    BrightnessTargets targets_ = BrightnessTargets::All;
    std::vector<BacklightDevice> devices_;
    bool available_ = false;
    bool useBrightnessctl_ = false;
};

} // namespace st
