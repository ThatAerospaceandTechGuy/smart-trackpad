#include "EvdevDecode.h"

#include <algorithm>

namespace st::evdev {

Decoder::Decoder(AxisRange x, AxisRange y) : x_(x), y_(y) {}

void Decoder::Input(int type, int code, int value) {
    if (type != kEventTypeAbs) {
        return;
    }

    switch (code) {
    case kAbsMtSlot:
        if (value >= 0 && value < kMaxSlots) {
            currentSlot_ = value;
        }
        break;
    case kAbsMtPositionX:
        slots_[currentSlot_].x = value;
        slots_[currentSlot_].active = true;
        break;
    case kAbsMtPositionY:
        slots_[currentSlot_].y = value;
        slots_[currentSlot_].active = true;
        break;
    case kAbsMtTrackingId:
        // A lifted finger reports id -1; the slot may be reused right away.
        slots_[currentSlot_] = Slot{};
        slots_[currentSlot_].active = value >= 0;
        break;
    case kAbsX:
        lastX_ = value;
        break;
    case kAbsY:
        lastY_ = value;
        break;
    default:
        break;
    }
}

Report Decoder::Commit(bool tipDown) const {
    int activeSlots = 0;
    int slotX = 0;
    int slotY = 0;
    for (const Slot& slot : slots_) {
        if (slot.active) {
            ++activeSlots;
            slotX = slot.x;
            slotY = slot.y;
        }
    }

    Report report;
    if (activeSlots > 1) {
        report.contact = hid::Contact::Multiple;
        return report;
    }
    if (activeSlots == 0 && !tipDown) {
        report.contact = hid::Contact::Up;
        return report;
    }

    report.contact = hid::Contact::Down;
    report.hasPosition = true;

    const int rawX = activeSlots == 1 ? slotX : lastX_;
    const int rawY = activeSlots == 1 ? slotY : lastY_;

    report.x = std::clamp(
        hid::NormalizeValue(rawX, x_.minimum, x_.maximum, 32), 0.0f, 1.0f);
    // evdev Y grows downward; the engine works bottom-up.
    report.y = std::clamp(
        1.0f - hid::NormalizeValue(rawY, y_.minimum, y_.maximum, 32), 0.0f, 1.0f);
    return report;
}

void Decoder::Reset() {
    for (Slot& slot : slots_) {
        slot = Slot{};
    }
    currentSlot_ = 0;
}

} // namespace st::evdev
