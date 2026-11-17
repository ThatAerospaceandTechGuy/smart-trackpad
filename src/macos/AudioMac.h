#pragma once

#include <string>

#include "Feedback.h"

namespace st {

// System output volume through Core Audio.
//
// Reads and writes the default output device directly instead of synthesising
// volume keys, so the level is absolute and the read-back is exact.
class CoreAudioVolume {
public:
    explicit CoreAudioVolume(LevelFeedbackFn feedback);
    ~CoreAudioVolume();

    CoreAudioVolume(const CoreAudioVolume&) = delete;
    CoreAudioVolume& operator=(const CoreAudioVolume&) = delete;

    bool Initialize();
    void SetTargetPercent(int percent);
    void ToggleMute();

    [[nodiscard]] bool Available() const noexcept { return available_; }

private:
    bool ResolveDevice();
    [[nodiscard]] bool ReadLevel(int* percent, bool* muted) const;
    void Report();

    LevelFeedbackFn feedback_;
    unsigned int device_ = 0;
    bool available_ = false;
};

} // namespace st
