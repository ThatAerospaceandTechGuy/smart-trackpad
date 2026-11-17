#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ContactTracker.h"
#include "SettingsTypes.h"

namespace st {

// Reads absolute touch coordinates from evdev and feeds the shared
// ContactTracker.  One thread polls every accepted device with a single
// epoll/poll set; there is no per-device thread and no busy loop.
class EvdevInput {
public:
    struct Callbacks {
        std::function<void(float x, float y)> onContactDown;
        std::function<void(float x, float y)> onContactMove;
        std::function<void()> onContactUp;
        std::function<void()> onCancel;
    };

    explicit EvdevInput(Callbacks callbacks);
    ~EvdevInput();

    EvdevInput(const EvdevInput&) = delete;
    EvdevInput& operator=(const EvdevInput&) = delete;

    // Opens every matching /dev/input/event* node.  Returns false when nothing
    // could be opened (usually a permissions or "no touchpad" problem) and
    // writes the reason to the log.
    bool Start(DigitizerDevices devices);
    void Stop();

    // Restarts discovery, for example after a device was hot-plugged.
    void Refresh(DigitizerDevices devices);

    // Drains pending reports.  Call from the event loop; a report pending on a
    // device is a reason to call it again.
    void HandleReadable();

    // Poll descriptors for the whole device set, for the event loop's poll().
    [[nodiscard]] std::vector<int> FileDescriptors() const;

    // Human readable summary for the log and the zone test window.
    [[nodiscard]] std::wstring DescribeDevices() const;

    // True when at least one device is open.
    [[nodiscard]] bool HasDevices() const;

private:
    struct Device;

    void CloseAll();
    bool OpenDevice(const std::string& path, DigitizerDevices devices);

    ContactTracker tracker_;
    std::vector<std::unique_ptr<Device>> devices_;
};

} // namespace st
