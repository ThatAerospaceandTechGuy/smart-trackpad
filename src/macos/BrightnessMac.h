#pragma once

#include "Feedback.h"
#include "SettingsTypes.h"

namespace st {

// Panel brightness.
//
// Uses the private DisplayServices framework, the same entry point macOS uses
// for the brightness keys.  It is loaded with dlopen so a missing framework only
// disables the brightness edge instead of preventing startup.
class MacBrightness {
public:
    explicit MacBrightness(LevelFeedbackFn feedback);
    ~MacBrightness();

    MacBrightness(const MacBrightness&) = delete;
    MacBrightness& operator=(const MacBrightness&) = delete;

    bool Initialize();
    void SetTargets(BrightnessTargets targets);
    void SetTargetPercent(int percent);

    [[nodiscard]] bool Available() const noexcept { return available_; }
    [[nodiscard]] bool IsInternal() const noexcept { return internal_; }

private:
    using GetBrightnessFn = int (*)(unsigned int display, float* brightness);
    using SetBrightnessFn = int (*)(unsigned int display, float brightness);

    void Apply(int percent);

    LevelFeedbackFn feedback_;
    GetBrightnessFn getBrightness_ = nullptr;
    SetBrightnessFn setBrightness_ = nullptr;
    unsigned int display_ = 0;
    BrightnessTargets targets_ = BrightnessTargets::All;
    bool available_ = false;
    bool internal_ = false;
};

} // namespace st
