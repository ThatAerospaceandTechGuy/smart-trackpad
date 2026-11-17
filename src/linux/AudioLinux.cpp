#include "AudioLinux.h"

#include "Log.h"
#include "Text.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace st {
namespace {

const char* const kSinkPlaceholder = "@DEFAULT_AUDIO_SINK@";
const char* const kPulseSink = "@DEFAULT_SINK@";

// Parses "Volume: 0.42" and "  Mute: yes" / " [MUTED]" from wpctl.
bool ParseWpctlLevel(const std::string& output, int* percent, bool* muted) {
    const std::size_t key = output.find("Volume:");
    if (key == std::string::npos) {
        return false;
    }

    const char* begin = output.c_str() + key + 7;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return false;
    }

    *percent = static_cast<int>(value * 100.0 + 0.5);
    *muted = output.find("MUTED") != std::string::npos;
    return true;
}

// Parses pactl's "Volume: front-left: 26214 /  40% / -23.88 dB" and "Mute: yes".
bool ParsePactlLevel(const std::string& output, int* percent, bool* muted) {
    const std::size_t key = output.find("Volume:");
    if (key == std::string::npos) {
        return false;
    }

    const std::size_t marker = output.find('%', key);
    if (marker == std::string::npos) {
        return false;
    }

    // Walk back to the first digit of the percentage.
    std::size_t begin = marker;
    while (begin > key && (std::isdigit(static_cast<unsigned char>(output[begin - 1])) != 0)) {
        --begin;
    }

    *percent = std::atoi(output.c_str() + begin);
    *muted = output.find("Mute: yes") != std::string::npos;
    return true;
}

} // namespace

std::string RunCommand(const std::string& command, int* exitCode) {
    std::string output;
    std::FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        if (exitCode != nullptr) {
            *exitCode = -1;
        }
        return output;
    }

    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (exitCode != nullptr) {
        *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    return output;
}

bool HasProgram(const char* name) {
    const std::string command = std::string("command -v ") + name + " >/dev/null 2>&1";
    int code = 1;
    (void)RunCommand(command, &code);
    return code == 0;
}

LinuxVolumeController::LinuxVolumeController(LevelFeedbackFn feedback)
    : feedback_(std::move(feedback)) {
    if (HasProgram("wpctl")) {
        backend_ = Backend::WirePlumber;
    } else if (HasProgram("pactl")) {
        backend_ = Backend::PulseAudio;
    }

    if (backend_ == Backend::None) {
        LogMessage(LogLevel::Warning,
                   L"Volume: neither wpctl (PipeWire) nor pactl (PulseAudio) is "
                   L"available; the volume edge will have no effect.");
        return;
    }

    LogFormat(LogLevel::Info, L"Volume: using %hs.",
              backend_ == Backend::WirePlumber ? "wpctl (PipeWire)" : "pactl (PulseAudio)");
    thread_ = std::jthread([this](std::stop_token token) { Run(token); });
}

LinuxVolumeController::~LinuxVolumeController() {
    if (thread_.joinable()) {
        thread_.request_stop();
        condition_.notify_all();
        thread_.join();
    }
}

const char* LinuxVolumeController::BackendName() const noexcept {
    switch (backend_) {
    case Backend::WirePlumber:
        return "wpctl";
    case Backend::PulseAudio:
        return "pactl";
    case Backend::None:
        break;
    }
    return "none";
}

void LinuxVolumeController::SetTargetPercent(int percent) {
    if (backend_ == Backend::None) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        pendingPercent_ = percent;
        hasWork_ = true;
    }
    condition_.notify_all();
}

void LinuxVolumeController::ToggleMute() {
    if (backend_ == Backend::None) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        pendingMute_ = true;
        hasWork_ = true;
    }
    condition_.notify_all();
}

void LinuxVolumeController::Run(std::stop_token token) {
    std::stop_callback wake(token, [this] { condition_.notify_all(); });

    while (!token.stop_requested()) {
        int percent = -1;
        bool muteToggle = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this, &token] {
                return hasWork_ || token.stop_requested();
            });
            if (token.stop_requested()) {
                return;
            }

            // Coalesce: only the last requested value matters.
            percent = pendingPercent_;
            muteToggle = pendingMute_;
            pendingPercent_ = -1;
            pendingMute_ = false;
            hasWork_ = false;
        }

        Apply(percent, muteToggle);
    }
}

void LinuxVolumeController::Apply(int percent, bool muteToggle) {
    if (muteToggle) {
        const std::string command =
            backend_ == Backend::WirePlumber
                ? std::string("wpctl set-mute ") + kSinkPlaceholder + " toggle"
                : std::string("pactl set-sink-mute ") + kPulseSink + " toggle";
        (void)RunCommand(command);
    }

    if (percent >= 0) {
        const std::string value = std::to_string(percent) + "%";
        const std::string command =
            backend_ == Backend::WirePlumber
                ? std::string("wpctl set-volume ") + kSinkPlaceholder + " " + value
                : std::string("pactl set-sink-volume ") + kPulseSink + " " + value;
        const int code = [&] {
            int exitCode = -1;
            (void)RunCommand(command, &exitCode);
            return exitCode;
        }();
        if (code != 0) {
            LogFormat(LogLevel::Warning, L"Volume: \"%hs\" failed (%d).", command.c_str(),
                      code);
            return;
        }
    }

    int actual = percent < 0 ? 0 : percent;
    bool muted = false;
    if (ReadLevel(&actual, &muted) && feedback_) {
        feedback_(LevelFeedback{actual, muted});
    }
}

bool LinuxVolumeController::ReadLevel(int* percent, bool* muted) const {
    const std::string command =
        backend_ == Backend::WirePlumber
            ? std::string("wpctl get-volume ") + kSinkPlaceholder
            : std::string("pactl get-sink-volume ") + kPulseSink;
    const std::string output = RunCommand(command + " 2>/dev/null");

    if (backend_ == Backend::WirePlumber) {
        return ParseWpctlLevel(output, percent, muted);
    }
    return ParsePactlLevel(output, percent, muted);
}

} // namespace st
