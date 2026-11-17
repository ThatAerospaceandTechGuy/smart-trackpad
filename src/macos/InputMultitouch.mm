#include "InputMultitouch.h"

#include "ContactTracker.h"
#include "Log.h"
#include "Text.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>

namespace st {
namespace {

// Layouts from the reverse-engineered MultitouchSupport interface.  Only the
// fields this program needs are named; the struct has more members, which is why
// the offsets must stay exactly as they are.
struct MTPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct MTVector {
    MTPoint position{};
    MTPoint velocity{};
};

struct MTFinger {
    std::int32_t frame = 0;
    MTVector normalized{};
    float size = 0.0f;
    std::int32_t fingerId = 0;
    float angle = 0.0f;
    float majorAxis = 0.0f;
    float minorAxis = 0.0f;
    MTVector absolute{};
    std::int32_t state = 0;
    std::int32_t unknown1 = 0;
    float unknown2 = 0.0f;
    float density = 0.0f;
};

struct MTContact {
    std::int32_t contactCount = 0;
    MTFinger fingers[1];
};

// Finger states used by the framework.
constexpr int kStateNotTracking = 7;

// The framework's callback has no context pointer, so the trampoline below needs
// the single instance.  A second instance would be a bug (one process owns the
// trackpad), so documenting it is enough.
MultitouchInput* g_instance = nullptr;

} // namespace

struct MultitouchInput::Api {
    using CreateDefaultFn = void* (*)();
    using RegisterCallbackFn = void (*)(void*, int (*)(void*, void*, int, double, int));
    using StartFn = void (*)(void*, int);
    using StopFn = void (*)(void*);
    using ReleaseFn = void (*)(void*);

    CreateDefaultFn createDefault = nullptr;
    RegisterCallbackFn registerCallback = nullptr;
    StartFn start = nullptr;
    StopFn stop = nullptr;
    ReleaseFn release = nullptr;
};

MultitouchInput::MultitouchInput(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

MultitouchInput::~MultitouchInput() {
    Stop();
    delete api_;
}

int MultitouchInput::ContactCallback(void*, void* data, int contactCount, double, int) {
    if (g_instance != nullptr) {
        g_instance->HandleContacts(data, contactCount);
    }
    return 0;
}

bool MultitouchInput::Start() {
    if (available_) {
        return true;
    }

    // Private framework; present on every macOS install with a trackpad driver.
    void* framework = dlopen(
        "/System/Library/PrivateFrameworks/MultitouchSupport.framework/"
        "MultitouchSupport",
        RTLD_LAZY);
    if (framework == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Input: MultitouchSupport.framework could not be loaded; edge "
                   L"gestures need the built-in trackpad.");
        return false;
    }

    auto* api = new Api();
    api->createDefault = reinterpret_cast<Api::CreateDefaultFn>(
        dlsym(framework, "MTDeviceCreateDefault"));
    api->registerCallback = reinterpret_cast<Api::RegisterCallbackFn>(
        dlsym(framework, "MTRegisterContactFrameCallback"));
    api->start = reinterpret_cast<Api::StartFn>(dlsym(framework, "MTDeviceStart"));
    api->stop = reinterpret_cast<Api::StopFn>(dlsym(framework, "MTDeviceStop"));
    api->release =
        reinterpret_cast<Api::ReleaseFn>(dlsym(framework, "MTDeviceRelease"));

    if (api->createDefault == nullptr || api->registerCallback == nullptr ||
        api->start == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Input: MultitouchSupport does not export the expected symbols; "
                   L"the framework changed in this macOS release.");
        delete api;
        return false;
    }

    void* device = api->createDefault();
    if (device == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Input: no multitouch device (a desktop Mac without a "
                   L"trackpad?).");
        delete api;
        return false;
    }

    api_ = api;
    device_ = device;
    g_instance = this;

    // The callback receives `this` as its device argument.
    api_->registerCallback(device_, &MultitouchInput::ContactCallback);
    api_->start(device_, 0);

    available_ = true;
    deviceSummary_ = L"built-in multitouch trackpad";

    LogMessage(LogLevel::Info,
               L"Input: multitouch device started (absolute finger positions).");
    return true;
}

void MultitouchInput::Stop() {
    if (device_ != nullptr && api_ != nullptr && api_->stop != nullptr) {
        api_->stop(device_);
    }
    // The device object is owned by the framework and released with the process.
    // The callback registration cannot be undone for a default device, so the
    // tracker stays alive for the lifetime of the object and the callback checks
    // `available_`.
    if (g_instance == this) {
        g_instance = nullptr;
    }
    available_ = false;
    device_ = nullptr;
}

void MultitouchInput::HandleContacts(const void* data, int contactCount) {
    if (!available_ || data == nullptr) {
        return;
    }

    const auto* contacts = static_cast<const MTContact*>(data);
    const int count = std::min(contactCount, contacts->contactCount);

    int tracked = 0;
    float sumX = 0.0f;
    float sumY = 0.0f;
    for (int index = 0; index < count; ++index) {
        const MTFinger& finger = contacts->fingers[index];
        if (finger.state >= kStateNotTracking) {
            continue;
        }
        ++tracked;
        // normalized.y grows upward on the trackpad's own axes; the engine wants
        // bottom-up coordinates, which is the same direction here, so only x
        // needs no flip.  The value is already 0..1.
        sumX += finger.normalized.position.x;
        sumY += finger.normalized.position.y;
    }

    ContactSample sample;
    if (tracked > 1) {
        sample.contact = hid::Contact::Multiple;
    } else if (tracked == 1) {
        sample.contact = hid::Contact::Down;
        sample.hasPosition = true;
        sample.x = std::clamp(sumX, 0.0f, 1.0f);
        sample.y = std::clamp(1.0f - sumY, 0.0f, 1.0f);
    } else {
        sample.contact = hid::Contact::Up;
    }
    sample.reportsTipSwitch = true;

    if (sample.contact == hid::Contact::Down) {
        if (!down_) {
            down_ = true;
            if (callbacks_.onContactDown) {
                callbacks_.onContactDown(sample.x, sample.y);
            }
        } else if (callbacks_.onContactMove) {
            callbacks_.onContactMove(sample.x, sample.y);
        }
    } else if (down_) {
        down_ = false;
        if (callbacks_.onContactUp) {
            callbacks_.onContactUp();
        }
    }
}

std::wstring MultitouchInput::DescribeDevices() const {
    return available_ ? deviceSummary_ : L"No multitouch trackpad is reporting.";
}

} // namespace st
