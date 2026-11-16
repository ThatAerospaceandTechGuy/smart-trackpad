#pragma once

#include <memory>

#include "GestureEngine.h"

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

private:
    static void PressVirtualKey(unsigned int virtualKey);

    std::unique_ptr<BrightnessController> brightness_;
    std::unique_ptr<VolumeController> volume_;
    std::unique_ptr<Osd> osd_;
};

} // namespace st
