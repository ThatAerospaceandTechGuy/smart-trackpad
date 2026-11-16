#pragma once

#include <memory>

#include "GestureEngine.h"
#include "SettingsTypes.h"

namespace st {

class BrightnessController;
class Osd;
class VolumeController;

class ActionDispatcher {
public:
    ActionDispatcher();
    ~ActionDispatcher();

    ActionDispatcher(const ActionDispatcher&) = delete;
    ActionDispatcher& operator=(const ActionDispatcher&) = delete;

    void Handle(EdgeAction action);

    // Absolute edge values in percent (0 = bottom, 100 = top).
    void SetVolumePercent(int percent);
    void SetBrightnessPercent(int percent);

    // Applies display and overlay preferences without recreating the workers.
    void SetBrightnessTargets(BrightnessTargets targets);
    void SetOsdShowPercentage(bool show);

private:
    static void PressVirtualKey(unsigned int virtualKey);
    static void TurnMonitorOff();

    // Declared first so the overlay outlives both controller workers, which
    // post their confirmed values to it.
    std::unique_ptr<Osd> osd_;
    std::unique_ptr<VolumeController> volume_;
    std::unique_ptr<BrightnessController> brightness_;
};

} // namespace st
