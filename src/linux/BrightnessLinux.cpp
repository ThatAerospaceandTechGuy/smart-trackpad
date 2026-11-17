#include "BrightnessLinux.h"

#include "AudioLinux.h"
#include "Log.h"
#include "Text.h"

#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace st {
namespace {

int ReadIntFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    int value = 0;
    if (!(file >> value)) {
        return 0;
    }
    return value;
}

} // namespace

LinuxBrightnessController::LinuxBrightnessController(LevelFeedbackFn feedback)
    : feedback_(std::move(feedback)) {
    std::error_code error;
    const std::filesystem::path root("/sys/class/backlight");
    if (!std::filesystem::exists(root, error)) {
        LogMessage(LogLevel::Warning,
                   L"Brightness: /sys/class/backlight does not exist; the brightness "
                   L"edge will have no effect.");
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        BacklightDevice device;
        device.directory = entry.path();
        device.name = entry.path().filename().string();
        device.maximum = ReadIntFile(device.directory / "max_brightness");
        device.writable = access((device.directory / "brightness").c_str(), W_OK) == 0;
        if (device.maximum > 0) {
            devices_.push_back(device);
        }
    }

    std::error_code sortError;
    std::sort(devices_.begin(), devices_.end(), [](const BacklightDevice& left,
                                                   const BacklightDevice& right) {
        return left.name < right.name;
    });
    (void)sortError;

    if (devices_.empty()) {
        LogMessage(LogLevel::Warning,
                   L"Brightness: no backlight device with a usable range was found.");
        return;
    }

    const bool anyWritable = std::any_of(devices_.begin(), devices_.end(),
                                         [](const BacklightDevice& device) {
                                             return device.writable;
                                         });

    if (!anyWritable) {
        // brightnessctl is setuid on most distributions, so it can write the
        // node we are not allowed to touch.
        if (HasProgram("brightnessctl")) {
            useBrightnessctl_ = true;
            LogMessage(LogLevel::Info,
                       L"Brightness: /sys/class/backlight is read-only for this user; "
                       L"falling back to brightnessctl.");
        } else {
            LogMessage(LogLevel::Warning,
                       L"Brightness: /sys/class/backlight is not writable and "
                       L"brightnessctl is not installed. Add a udev rule or install "
                       L"brightnessctl; the brightness edge has no effect until then.");
        }
    }

    available_ = anyWritable || useBrightnessctl_;
    if (available_) {
        LogFormat(LogLevel::Info, L"Brightness: %d backlight device(s) (%hs).",
                  static_cast<int>(devices_.size()), BackendName());
    }
}

LinuxBrightnessController::~LinuxBrightnessController() = default;

const char* LinuxBrightnessController::BackendName() const noexcept {
    return useBrightnessctl_ ? "brightnessctl" : "sysfs";
}

void LinuxBrightnessController::SetTargets(BrightnessTargets targets) {
    targets_ = targets;
}

void LinuxBrightnessController::SetTargetPercent(int percent) {
    if (!available_) {
        return;
    }
    Apply(percent);
}

void LinuxBrightnessController::Apply(int percent) {
    const int clamped = std::clamp(percent, 0, 100);

    if (useBrightnessctl_) {
        const std::string command = "brightnessctl -q set " + std::to_string(clamped) + "%";
        int code = -1;
        (void)RunCommand(command, &code);
        if (code != 0) {
            LogFormat(LogLevel::Warning, L"Brightness: \"%hs\" failed (%d).",
                      command.c_str(), code);
            return;
        }
        if (feedback_) {
            feedback_(LevelFeedback{clamped, false});
        }
        return;
    }

    // Every writable backlight follows the same value so a laptop with two
    // panels (or a panel plus a keyboard backlight classified as backlight)
    // stays consistent.
    int applied = clamped;
    for (const BacklightDevice& device : devices_) {
        if (!device.writable) {
            continue;
        }

        const int raw = std::max(0, device.maximum * clamped / 100);
        std::ofstream file(device.directory / "brightness", std::ios::trunc);
        if (!file) {
            LogFormat(LogLevel::Warning, L"Brightness: cannot write \"%ls\".",
                      (device.directory / "brightness").c_str());
            continue;
        }
        file << raw;
        file.flush();
        if (!file.good()) {
            LogFormat(LogLevel::Warning, L"Brightness: write to \"%ls\" failed.",
                      (device.directory / "brightness").c_str());
        }

        // Report what the kernel actually holds, not what was requested.
        const int readBack = ReadIntFile(device.directory / "brightness");
        applied = std::clamp(readBack * 100 / std::max(1, device.maximum), 0, 100);
    }

    if (feedback_) {
        feedback_(LevelFeedback{applied, false});
    }
}

} // namespace st
