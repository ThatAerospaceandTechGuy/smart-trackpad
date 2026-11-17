#pragma once

#include "HidDecode.h"

// Decoding of one evdev multitouch report into a normalized contact.  Split out
// of the reader so the slot bookkeeping and the coordinate mapping are unit
// tested on any platform, the same way HidDecode covers the HID side.
namespace st::evdev {

// Event type and code values from linux/input-event-codes.h, repeated here so
// the decoder (and its test) build on any platform.  InputEvdev.cpp asserts that
// they match the kernel headers it includes.
inline constexpr int kEventTypeAbs = 0x03;
inline constexpr int kAbsX = 0x00;
inline constexpr int kAbsY = 0x01;
inline constexpr int kAbsMtSlot = 0x2f;
inline constexpr int kAbsMtPositionX = 0x35;
inline constexpr int kAbsMtPositionY = 0x36;
inline constexpr int kAbsMtTrackingId = 0x39;

struct AxisRange {
    int minimum = 0;
    int maximum = 0;
};

// What one completed report (one SYN_REPORT) means to the gesture layer.
struct Report {
    hid::Contact contact = hid::Contact::Up;
    bool hasPosition = false;
    // Normalized 0..1; y is flipped to bottom-up, the engine's convention.
    float x = 0.0f;
    float y = 0.0f;
};

// Tracks the multitouch slots of one device.
class Decoder {
public:
    static constexpr int kMaxSlots = 10;

    Decoder(AxisRange x, AxisRange y);

    // Accumulates one evdev event.  Non-positional events are ignored; call
    // Commit() when SYN_REPORT arrives.
    void Input(int type, int code, int value);

    // `tipDown` is the device's BTN_TOUCH state.
    [[nodiscard]] Report Commit(bool tipDown) const;

    // Drops every slot, for example after the device disappeared.
    void Reset();

private:
    struct Slot {
        bool active = false;
        int x = 0;
        int y = 0;
    };

    AxisRange x_;
    AxisRange y_;
    Slot slots_[kMaxSlots];
    int currentSlot_ = 0;
    int lastX_ = 0;
    int lastY_ = 0;
};

} // namespace st::evdev
