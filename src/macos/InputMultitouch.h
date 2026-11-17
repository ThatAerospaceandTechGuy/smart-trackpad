#pragma once

#include <functional>
#include <string>

namespace st {

// Absolute finger positions from the built-in trackpad.
//
// macOS does not expose trackpad digitizer data through a public API: the
// multitouch stack is the private MultitouchSupport.framework, which is what
// every third-party gesture tool (BetterTouchTool, MiddleClick, ...) uses.  It
// is loaded at runtime with dlopen, so the program still starts (and logs why)
// on a system where the framework is missing or has changed.
//
// Positions arrive normalized to the pad: x 0..1 left to right, y 0..1 bottom
// to top, already flipped into the engine's coordinate space.
class MultitouchInput {
public:
    struct Callbacks {
        std::function<void(float x, float y)> onContactDown;
        std::function<void(float x, float y)> onContactMove;
        std::function<void()> onContactUp;
        std::function<void()> onCancel;
    };

    explicit MultitouchInput(Callbacks callbacks);
    ~MultitouchInput();

    MultitouchInput(const MultitouchInput&) = delete;
    MultitouchInput& operator=(const MultitouchInput&) = delete;

    // True when the framework was loaded and a device was started.
    bool Start();
    void Stop();

    [[nodiscard]] bool Available() const noexcept { return available_; }
    [[nodiscard]] std::wstring DescribeDevices() const;

private:
    struct Api;
    static int ContactCallback(void* device, void* data, int contactCount,
                               double timestamp, int frame);
    void HandleContacts(const void* data, int contactCount);

    Callbacks callbacks_;
    Api* api_ = nullptr;
    void* device_ = nullptr;
    bool available_ = false;
    bool down_ = false;
    std::wstring deviceSummary_;
};

} // namespace st
