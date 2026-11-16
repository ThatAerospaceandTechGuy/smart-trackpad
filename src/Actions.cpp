#include "Actions.h"

#include "Brightness.h"
#include "Feedback.h"
#include "Log.h"
#include "Osd.h"
#include "Volume.h"

#include <windows.h>

#include <utility>

namespace st {

ActionDispatcher::ActionDispatcher()
    : osd_(std::make_unique<Osd>()) {
    // The controls show the requested level immediately (the gesture engine is
    // the responsive path) and then converge on what the hardware reports.  The
    // controller workers post to the overlay, so this is thread safe and the
    // overlay is declared before them.
    LevelFeedbackFn onVolumeApplied = [this](const LevelFeedback& feedback) {
        if (osd_) {
            osd_->Post(OsdKind::Volume, feedback.percent, feedback.muted);
        }
    };
    LevelFeedbackFn onBrightnessApplied = [this](const LevelFeedback& feedback) {
        if (osd_) {
            osd_->Post(OsdKind::Brightness, feedback.percent, false);
        }
    };

    volume_ = std::make_unique<VolumeController>(std::move(onVolumeApplied));
    brightness_ =
        std::make_unique<BrightnessController>(std::move(onBrightnessApplied));
}

ActionDispatcher::~ActionDispatcher() = default;

void ActionDispatcher::Handle(EdgeAction action) {
    switch (action) {
    case EdgeAction::MediaNext:
        PressVirtualKey(VK_MEDIA_NEXT_TRACK);
        break;
    case EdgeAction::MediaPrevious:
        PressVirtualKey(VK_MEDIA_PREV_TRACK);
        break;
    case EdgeAction::MediaPlayPause:
        PressVirtualKey(VK_MEDIA_PLAY_PAUSE);
        break;
    case EdgeAction::ToggleMute:
        volume_->ToggleMute();
        break;
    case EdgeAction::MonitorOff:
        TurnMonitorOff();
        break;
    }
}

void ActionDispatcher::TurnMonitorOff() {
    // Broadcast the display-power request; any key or touch wakes the screen.
    DWORD_PTR result = 0;
    const LRESULT sent = SendMessageTimeoutW(
        HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER,
        static_cast<LPARAM>(2 /* power off */), SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG,
        1000, &result);
    LogFormat(LogLevel::Debug, L"Monitor off request returned %lld.",
              static_cast<long long>(sent));
}

void ActionDispatcher::SetVolumePercent(int percent) {
    volume_->SetTargetPercent(percent);
    if (osd_) {
        osd_->Show(OsdKind::Volume, percent);
    }
}

void ActionDispatcher::SetBrightnessPercent(int percent) {
    brightness_->SetTargetPercent(percent);
    if (osd_) {
        osd_->Show(OsdKind::Brightness, percent);
    }
}

void ActionDispatcher::PressVirtualKey(unsigned int virtualKey) {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = static_cast<WORD>(virtualKey);
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    const UINT sent = SendInput(2, inputs, sizeof(INPUT));
    if (sent != 2) {
        LogFormat(LogLevel::Debug,
                  L"SendInput for VK 0x%02X injected %u/2 events (error %lu).",
                  virtualKey, sent, GetLastError());
    }
}

} // namespace st
