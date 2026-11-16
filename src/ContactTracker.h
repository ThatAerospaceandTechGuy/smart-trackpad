#pragma once

#include <functional>
#include <unordered_map>

#include "HidDecode.h"

namespace st {

// One decoded digitizer report, reduced to what the gesture lifecycle needs.
// RawInputSource fills this in from the HID parser; tests fill it in directly,
// which is why the lifecycle rules below are testable without a touchpad.
struct ContactSample {
    hid::Contact contact = hid::Contact::Unknown;
    bool hasPosition = false;
    float x = 0.0f;
    float y = 0.0f;
    // Whether the device exposes a tip switch at all.  A digitizer without one
    // signals a release by omitting the position.
    bool reportsTipSwitch = true;
};

// Turns a stream of digitizer reports into contact-down / move / up / cancel
// callbacks.  Only one contact is tracked; a multi-finger report cancels the
// gesture and blocks the device until every finger is lifted.
class ContactTracker {
public:
    struct Callbacks {
        std::function<void(float x, float y)> onDown;
        std::function<void(float x, float y)> onMove;
        std::function<void()> onUp;
        std::function<void()> onCancel;
    };

    explicit ContactTracker(Callbacks callbacks);

    ContactTracker(const ContactTracker&) = delete;
    ContactTracker& operator=(const ContactTracker&) = delete;

    // `deviceKey` identifies the HID device; a report from a second device
    // while another one is active is ignored.
    void Feed(const void* deviceKey, const ContactSample& sample);

    // The device disappeared (for example a USB removal while touching).
    void DeviceRemoved(const void* deviceKey);

    void Reset();

    [[nodiscard]] bool IsTracking() const noexcept { return activeDevice_ != nullptr; }

private:
    struct DeviceState {
        bool blocked = false;
    };

    void CancelActive();

    Callbacks callbacks_;
    const void* activeDevice_ = nullptr;
    std::unordered_map<const void*, DeviceState> devices_;
};

} // namespace st
