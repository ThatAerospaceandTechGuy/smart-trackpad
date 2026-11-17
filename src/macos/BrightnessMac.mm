#include "BrightnessMac.h"

#include "Log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace st {
namespace {

// The main display is display 0 for the private API; CGMainDisplayID() returns a
// CGDirectDisplayID, which is what the framework expects.
unsigned int MainDisplay() {
    return static_cast<unsigned int>(CGMainDisplayID());
}

} // namespace

MacBrightness::MacBrightness(LevelFeedbackFn feedback)
    : feedback_(std::move(feedback)) {}

MacBrightness::~MacBrightness() = default;

bool MacBrightness::Initialize() {
    void* framework = dlopen(
        "/System/Library/PrivateFrameworks/DisplayServices.framework/DisplayServices",
        RTLD_LAZY);
    if (framework == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Brightness: DisplayServices.framework is unavailable; the "
                   L"brightness edge has no effect on this system.");
        return false;
    }

    getBrightness_ = reinterpret_cast<GetBrightnessFn>(
        dlsym(framework, "DisplayServicesGetBrightness"));
    setBrightness_ = reinterpret_cast<SetBrightnessFn>(
        dlsym(framework, "DisplayServicesSetBrightness"));

    if (getBrightness_ == nullptr || setBrightness_ == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Brightness: DisplayServices does not export the brightness "
                   L"functions; the brightness edge has no effect.");
        return false;
    }

    display_ = MainDisplay();

    float current = 0.0f;
    if (getBrightness_(display_, &current) != 0) {
        LogMessage(LogLevel::Warning,
                   L"Brightness: the main display does not report a brightness "
                   L"(an external monitor?). The brightness edge has no effect.");
        return false;
    }

    internal_ = CGDisplayIsBuiltin(display_) != 0;
    available_ = true;

    LogFormat(LogLevel::Info,
              L"Brightness: DisplayServices on the %ls display (currently %.0f%%).",
              internal_ ? L"built-in" : L"main", static_cast<double>(current * 100.0f));
    return true;
}

void MacBrightness::SetTargets(BrightnessTargets targets) {
    targets_ = targets;
}

void MacBrightness::SetTargetPercent(int percent) {
    if (!available_) {
        return;
    }
    Apply(percent);
}

void MacBrightness::Apply(int percent) {
    if (targets_ == BrightnessTargets::Internal && !internal_) {
        return;
    }

    const float scalar =
        std::clamp(static_cast<float>(percent) / 100.0f, 0.0f, 1.0f);
    if (setBrightness_(display_, scalar) != 0) {
        LogFormat(LogLevel::Warning, L"Brightness: setting %.0f%% failed.",
                  static_cast<double>(percent));
        return;
    }

    // Report what the display actually holds.
    float applied = scalar;
    if (getBrightness_(display_, &applied) != 0) {
        applied = scalar;
    }
    if (feedback_) {
        feedback_(LevelFeedback{
            static_cast<int>(std::lround(applied * 100.0f)), false});
    }
}

} // namespace st
