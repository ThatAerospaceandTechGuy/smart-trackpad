#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "Feedback.h"

namespace st {

// Runs a command and returns its standard output (empty on failure).
[[nodiscard]] std::string RunCommand(const std::string& command, int* exitCode = nullptr);

// True when the program is on PATH.
[[nodiscard]] bool HasProgram(const char* name);

// System volume through PipeWire (wpctl) or PulseAudio (pactl).
//
// No library is linked: both tools are part of every desktop install that has
// a sound server, and talking to them over a pipe keeps this file free of audio
// dependencies.  Writes happen on a worker thread and are coalesced, so a slow
// server never delays gesture handling.
class LinuxVolumeController {
public:
    explicit LinuxVolumeController(LevelFeedbackFn feedback);
    ~LinuxVolumeController();

    LinuxVolumeController(const LinuxVolumeController&) = delete;
    LinuxVolumeController& operator=(const LinuxVolumeController&) = delete;

    void SetTargetPercent(int percent);
    void ToggleMute();
    [[nodiscard]] bool Available() const noexcept { return backend_ != Backend::None; }
    [[nodiscard]] const char* BackendName() const noexcept;

private:
    enum class Backend { None, WirePlumber, PulseAudio };

    void Run(std::stop_token token);
    void Apply(int percent, bool muteToggle);
    [[nodiscard]] bool ReadLevel(int* percent, bool* muted) const;

    LevelFeedbackFn feedback_;
    Backend backend_ = Backend::None;
    std::string sinkName_;

    std::mutex mutex_;
    std::condition_variable_any condition_;
    int pendingPercent_ = -1;
    bool pendingMute_ = false;
    bool hasWork_ = false;
    std::jthread thread_;
};

} // namespace st
