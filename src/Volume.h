#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include "Feedback.h"

namespace st {

// Applies absolute system volume without blocking the message loop.
// Uses the default multimedia render endpoint through Core Audio.
class VolumeController {
public:
    explicit VolumeController(LevelFeedbackFn onApplied = {});
    ~VolumeController();

    VolumeController(const VolumeController&) = delete;
    VolumeController& operator=(const VolumeController&) = delete;

    // 0 = silent, 100 = maximum.  Non-blocking; only the newest value is
    // applied, and bursts are coalesced on the worker thread.
    void SetTargetPercent(int percent);

    // Flips the endpoint mute state; used by a tap on the volume edge.
    void ToggleMute();

private:
    // COM/Core Audio state lives only on the volume worker.  It is released
    // before the worker uninitializes COM.
    struct AudioState;

    void Run(std::stop_token stop);

    LevelFeedbackFn onApplied_;

    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    int pendingPercent_ = -1;
    bool pendingMuteToggle_ = false;
    std::unique_ptr<AudioState> audio_;
};

} // namespace st
