#pragma once

#include <windows.h>

#include <hidsdi.h>
#include <hidpi.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace st {

// Receives absolute HID digitizer contacts (Windows Precision Touchpad or
// touchscreen collections) through a background message-only window.
class RawInputSource {
public:
    struct Callbacks {
        std::function<void(float, float)> onContactDown;
        std::function<void(float, float)> onContactMove;
        std::function<void()> onContactUp;
        std::function<void()> onCancel;
    };

    explicit RawInputSource(Callbacks callbacks);
    ~RawInputSource();

    RawInputSource(const RawInputSource&) = delete;
    RawInputSource& operator=(const RawInputSource&) = delete;

    // Registers for HID touchpad/touchscreen top-level collections on the
    // caller's window (created with HWND_MESSAGE) with RIDEV_INPUTSINK.
    bool Register(HWND target);
    void Unregister();

    void HandleRawInput(HRAWINPUT rawInput);
    void HandleDeviceChange(WPARAM change, HANDLE device);

private:
    struct Device {
        std::wstring name;
        std::unique_ptr<std::byte[]> preparsedStorage;
        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};

        bool isDigitizer = false;
        bool hasX = false;
        bool hasY = false;
        LONG logicalXMin = 0;
        LONG logicalXMax = 0;
        LONG logicalYMin = 0;
        LONG logicalYMax = 0;
        USHORT xBitSize = 0;
        USHORT yBitSize = 0;

        bool hasTipSwitch = false;
        bool hasContactCount = false;
        std::vector<USAGE> usageScratch;

        // Latched after a multi-finger report; the pad must be fully released
        // before a new one-finger gesture can start.
        bool blocked = false;
    };

    Device* GetDevice(HANDLE device);
    void ExamineDevice(Device& device, HANDLE handle);
    void HandleHidReport(HANDLE deviceHandle, const BYTE* report, UINT length);
    void HandleDigitizerReport(HANDLE deviceHandle, Device& device, const BYTE* report,
                               UINT length);
    void CancelActive();

    Callbacks callbacks_;
    bool registered_ = false;
    HANDLE activeDevice_ = nullptr;
    std::unordered_map<HANDLE, std::unique_ptr<Device>> devices_;
};

} // namespace st
