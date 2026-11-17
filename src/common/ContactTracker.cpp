#include "ContactTracker.h"

#include <utility>

namespace st {

ContactTracker::ContactTracker(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

void ContactTracker::Feed(const void* deviceKey, const ContactSample& sample) {
    DeviceState& device = devices_[deviceKey];

    // Two or more fingers: drop the gesture and wait for a full release so the
    // palm that caused it cannot immediately start a new one.
    if (sample.contact == hid::Contact::Multiple) {
        CancelActive();
        device.blocked = true;
        return;
    }

    if (sample.contact == hid::Contact::Up) {
        if (activeDevice_ == deviceKey) {
            activeDevice_ = nullptr;
            if (callbacks_.onUp) {
                callbacks_.onUp();
            }
        }
        device.blocked = false;
        return;
    }

    if (sample.contact == hid::Contact::Unknown && !sample.hasPosition) {
        // A digitizer without a tip switch signals release by omitting the
        // position entirely.
        if (!sample.reportsTipSwitch) {
            if (activeDevice_ == deviceKey) {
                activeDevice_ = nullptr;
                if (callbacks_.onUp) {
                    callbacks_.onUp();
                }
            }
            device.blocked = false;
        }
        return;
    }

    if (!sample.hasPosition) {
        // The tip is down but this report carries no coordinates.
        return;
    }

    if (device.blocked) {
        return;
    }

    if (activeDevice_ != nullptr && activeDevice_ != deviceKey) {
        return;
    }

    if (activeDevice_ == nullptr) {
        activeDevice_ = deviceKey;
        if (callbacks_.onDown) {
            callbacks_.onDown(sample.x, sample.y);
        }
    } else if (callbacks_.onMove) {
        callbacks_.onMove(sample.x, sample.y);
    }
}

void ContactTracker::DeviceRemoved(const void* deviceKey) {
    if (activeDevice_ == deviceKey) {
        CancelActive();
    }
    devices_.erase(deviceKey);
}

void ContactTracker::Reset() {
    CancelActive();
    devices_.clear();
}

void ContactTracker::CancelActive() {
    if (activeDevice_ == nullptr) {
        return;
    }

    devices_[activeDevice_].blocked = true;
    activeDevice_ = nullptr;

    if (callbacks_.onCancel) {
        callbacks_.onCancel();
    }
}

} // namespace st
