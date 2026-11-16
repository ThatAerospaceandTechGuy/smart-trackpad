#include "RawInput.h"

#include "HidDecode.h"
#include "Log.h"

#include <windows.h>
#include <hidpi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace st {
namespace {

// HID usage identifiers from the HID Usage Tables.  These are fixed by the
// HID specification, so the numeric values are used directly instead of
// depending on a particular SDK's header spellings.
constexpr USAGE kUsagePageGenericDesktop = 0x01;
constexpr USAGE kUsagePageDigitizer = 0x0D;

constexpr USAGE kUsageTouchScreen = 0x04;
constexpr USAGE kUsageTouchPad = 0x05;
constexpr USAGE kUsageX = 0x30;
constexpr USAGE kUsageY = 0x31;
constexpr USAGE kUsageTipSwitch = 0x42;
constexpr USAGE kUsageContactCount = 0x54;

constexpr UINT kStackBufferSize = 1024;

bool ValueCapsMatch(const HIDP_VALUE_CAPS& caps, USAGE page, USAGE usage) {
    if (caps.UsagePage != page) {
        return false;
    }
    if (caps.IsRange) {
        return usage >= caps.Range.UsageMin && usage <= caps.Range.UsageMax;
    }
    return caps.NotRange.Usage == usage;
}

bool ButtonCapsMatch(const HIDP_BUTTON_CAPS& caps, USAGE page, USAGE usage) {
    if (caps.UsagePage != page) {
        return false;
    }
    if (caps.IsRange) {
        return usage >= caps.Range.UsageMin && usage <= caps.Range.UsageMax;
    }
    return caps.NotRange.Usage == usage;
}

} // namespace

RawInputSource::RawInputSource(Callbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

RawInputSource::~RawInputSource() {
    Unregister();
}

bool RawInputSource::Register(HWND target) {
    constexpr struct {
        USAGE usage;
        const wchar_t* name;
    } collections[] = {
        {kUsageTouchPad, L"touchpad"},
        {kUsageTouchScreen, L"touchscreen"},
    };

    bool registeredAny = false;

    for (const auto& collection : collections) {
        RAWINPUTDEVICE device{};
        device.usUsagePage = kUsagePageDigitizer;
        device.usUsage = collection.usage;
        device.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        device.hwndTarget = target;

        if (RegisterRawInputDevices(&device, 1, sizeof(device))) {
            registeredAny = true;
            LogFormat(LogLevel::Debug,
                      L"Raw input: registered %ls collection (0x%04X/0x%04X).",
                      collection.name, kUsagePageDigitizer, collection.usage);
        } else {
            LogFormat(LogLevel::Warning,
                      L"Raw input: registration failed for %ls (0x%04X/0x%04X), "
                      L"error %lu.",
                      collection.name, kUsagePageDigitizer, collection.usage,
                      GetLastError());
        }
    }

    registered_ = registeredAny;
    if (registeredAny) {
        LogMessage(LogLevel::Info,
                   L"Raw input: digitizer collections registered (background sink).");
    }
    return registeredAny;
}

void RawInputSource::Unregister() {
    CancelActive();

    if (registered_) {
        const USAGE usages[] = {kUsageTouchPad, kUsageTouchScreen};
        for (USAGE usage : usages) {
            RAWINPUTDEVICE device{};
            device.usUsagePage = kUsagePageDigitizer;
            device.usUsage = usage;
            device.dwFlags = RIDEV_REMOVE;
            device.hwndTarget = nullptr;
            RegisterRawInputDevices(&device, 1, sizeof(device));
        }
        registered_ = false;
    }

    devices_.clear();
}

void RawInputSource::HandleRawInput(HRAWINPUT rawInput) {
    if (rawInput == nullptr) {
        return;
    }

    UINT size = 0;
    if (GetRawInputData(rawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0) {
        return;
    }

    std::array<BYTE, kStackBufferSize> stackBuffer{};
    std::vector<BYTE> heapBuffer;
    BYTE* buffer = stackBuffer.data();
    if (size > stackBuffer.size()) {
        heapBuffer.resize(size);
        buffer = heapBuffer.data();
    }

    const UINT read =
        GetRawInputData(rawInput, RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
    if (read != size) {
        return;
    }

    const auto* input = reinterpret_cast<const RAWINPUT*>(buffer);
    if (input->header.dwType != RIM_TYPEHID || input->header.hDevice == nullptr) {
        return;
    }

    const UINT reportSize = input->data.hid.dwSizeHid;
    const UINT reportCount = input->data.hid.dwCount;
    if (reportSize == 0 || reportCount == 0) {
        return;
    }

    // A single WM_INPUT can bundle several HID reports.  They are laid out
    // consecutively, each with its own report ID byte (when the device uses
    // report IDs).  That is exactly the layout the HidP_* parsers expect.
    for (UINT index = 0; index < reportCount; ++index) {
        const BYTE* report = input->data.hid.bRawData + index * reportSize;
        HandleHidReport(input->header.hDevice, report, reportSize);
    }
}

void RawInputSource::HandleDeviceChange(WPARAM change, HANDLE device) {
    if (change == GIDC_REMOVAL) {
        if (activeDevice_ == device) {
            CancelActive();
        }
        devices_.erase(device);
    }
}

RawInputSource::Device* RawInputSource::GetDevice(HANDLE handle) {
    const auto found = devices_.find(handle);
    if (found != devices_.end()) {
        return found->second.get();
    }

    auto device = std::make_unique<Device>();
    ExamineDevice(*device, handle);

    Device* raw = device.get();
    devices_.emplace(handle, std::move(device));
    return raw;
}

void RawInputSource::ExamineDevice(Device& device, HANDLE handle) {
    // Device name (character count, not bytes).
    UINT nameCharacters = 0;
    GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, nullptr, &nameCharacters);
    if (nameCharacters > 0) {
        std::wstring name(nameCharacters, L'\0');
        UINT nameSize = nameCharacters;
        if (GetRawInputDeviceInfoW(handle, RIDI_DEVICENAME, name.data(), &nameSize) !=
            static_cast<UINT>(-1)) {
            while (!name.empty() && name.back() == L'\0') {
                name.pop_back();
            }
            device.name = std::move(name);
        }
    }
    if (device.name.empty()) {
        device.name = L"<unknown HID collection>";
    }

    // RIDI_PREPARSEDDATA returns a copy in a caller-supplied buffer; the
    // buffer must stay alive for every later HidP_* call.  A null pData query
    // returns 0 and stores the required size in preparsedSize, so only the
    // size is meaningful here.
    UINT preparsedSize = 0;
    GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA, nullptr, &preparsedSize);
    if (preparsedSize == 0) {
        LogFormat(LogLevel::Warning, L"HID: no preparsed data for \"%ls\".",
                  device.name.c_str());
        return;
    }

    device.preparsedStorage = std::make_unique<std::byte[]>(preparsedSize);
    UINT actualSize = preparsedSize;
    if (GetRawInputDeviceInfoW(handle, RIDI_PREPARSEDDATA,
                               device.preparsedStorage.get(), &actualSize) ==
        static_cast<UINT>(-1)) {
        device.preparsedStorage.reset();
        return;
    }

    device.preparsed =
        reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsedStorage.get());

    if (HidP_GetCaps(device.preparsed, &device.caps) != HIDP_STATUS_SUCCESS) {
        device.preparsed = nullptr;
        device.preparsedStorage.reset();
        return;
    }

    device.isDigitizer =
        device.caps.UsagePage == kUsagePageDigitizer &&
        (device.caps.Usage == kUsageTouchPad || device.caps.Usage == kUsageTouchScreen);

    if (!device.isDigitizer) {
        LogFormat(LogLevel::Debug,
                  L"HID: ignoring \"%ls\" (usage page 0x%04X, usage 0x%04X).",
                  device.name.c_str(), device.caps.UsagePage, device.caps.Usage);
        return;
    }

    USHORT valueCapsCount = 0;
    HidP_GetValueCaps(HidP_Input, nullptr, &valueCapsCount, device.preparsed);
    if (valueCapsCount > 0) {
        std::vector<HIDP_VALUE_CAPS> valueCaps(valueCapsCount);
        if (HidP_GetValueCaps(HidP_Input, valueCaps.data(), &valueCapsCount,
                              device.preparsed) == HIDP_STATUS_SUCCESS) {
            for (const HIDP_VALUE_CAPS& caps : valueCaps) {
                if (ValueCapsMatch(caps, kUsagePageGenericDesktop, kUsageX) &&
                    !device.hasX) {
                    device.hasX = true;
                    device.logicalXMin = caps.LogicalMin;
                    device.logicalXMax = caps.LogicalMax;
                    device.xBitSize = caps.BitSize;
                } else if (ValueCapsMatch(caps, kUsagePageGenericDesktop, kUsageY) &&
                           !device.hasY) {
                    device.hasY = true;
                    device.logicalYMin = caps.LogicalMin;
                    device.logicalYMax = caps.LogicalMax;
                    device.yBitSize = caps.BitSize;
                } else if (ValueCapsMatch(caps, kUsagePageDigitizer, kUsageContactCount)) {
                    device.hasContactCount = true;
                }
            }
        }
    }

    if (!device.hasX || !device.hasY) {
        LogFormat(LogLevel::Warning,
                  L"HID digitizer \"%ls\" has no absolute X/Y usages; ignoring it.",
                  device.name.c_str());
        device.isDigitizer = false;
        return;
    }

    USHORT buttonCapsCount = 0;
    HidP_GetButtonCaps(HidP_Input, nullptr, &buttonCapsCount, device.preparsed);
    if (buttonCapsCount > 0) {
        std::vector<HIDP_BUTTON_CAPS> buttonCaps(buttonCapsCount);
        if (HidP_GetButtonCaps(HidP_Input, buttonCaps.data(), &buttonCapsCount,
                               device.preparsed) == HIDP_STATUS_SUCCESS) {
            for (const HIDP_BUTTON_CAPS& caps : buttonCaps) {
                if (ButtonCapsMatch(caps, kUsagePageDigitizer, kUsageTipSwitch)) {
                    device.hasTipSwitch = true;
                    break;
                }
            }
        }
    }

    // Reused for HidP_GetUsages; kept on the device to avoid per-report heap
    // traffic.
    const ULONG maxUsages = std::min<ULONG>(
        HidP_MaxUsageListLength(HidP_Input, kUsagePageDigitizer, device.preparsed), 256);
    device.usageScratch.assign(std::max<ULONG>(maxUsages, 8), 0);

    LogFormat(LogLevel::Info,
              L"HID digitizer: \"%ls\" (usage 0x%04X/0x%04X, X %ld..%ld, "
              L"Y %ld..%ld, tip switch %ls, contact count %ls).",
              device.name.c_str(), device.caps.UsagePage, device.caps.Usage,
              device.logicalXMin, device.logicalXMax, device.logicalYMin,
              device.logicalYMax, device.hasTipSwitch ? L"yes" : L"no",
              device.hasContactCount ? L"yes" : L"no");
}

void RawInputSource::HandleHidReport(HANDLE deviceHandle, const BYTE* report,
                                     UINT length) {
    Device* device = GetDevice(deviceHandle);
    if (device == nullptr || !device->isDigitizer || device->preparsed == nullptr) {
        return;
    }

    HandleDigitizerReport(deviceHandle, *device, report, length);
}

void RawInputSource::HandleDigitizerReport(HANDLE deviceHandle, Device& device,
                                           const BYTE* report, UINT length) {
    auto* reportData = const_cast<PCHAR>(reinterpret_cast<const char*>(report));

    // ------------------------------------------------------------------
    // Contact state.  Tip Switch (buttons) and Contact Count (a value) are
    // queried before X/Y so a release report is honoured even if it carries
    // no coordinates.  The combination rule lives in HidDecode so it is unit
    // tested independently of the HID plumbing.
    // ------------------------------------------------------------------
    hid::ContactSignals signals;

    if (device.hasTipSwitch) {
        ULONG usageCount = static_cast<ULONG>(device.usageScratch.size());
        const NTSTATUS status =
            HidP_GetUsages(HidP_Input, kUsagePageDigitizer, 0,
                           device.usageScratch.data(), &usageCount, device.preparsed,
                           reportData, length);

        if (status == HIDP_STATUS_SUCCESS) {
            signals.tipQuery = hid::TipQuery::Ok;
            for (ULONG index = 0; index < usageCount; ++index) {
                if (device.usageScratch[index] == kUsageTipSwitch) {
                    ++signals.tipCount;
                }
            }
        } else if (status == HIDP_STATUS_BUFFER_TOO_SMALL) {
            signals.tipQuery = hid::TipQuery::BufferTooSmall;
        } else {
            signals.tipQuery = hid::TipQuery::Failed;
        }
    }

    if (device.hasContactCount) {
        ULONG contactCount = 0;
        if (HidP_GetUsageValue(HidP_Input, kUsagePageDigitizer, 0,
                               kUsageContactCount, &contactCount, device.preparsed,
                               reportData, length) == HIDP_STATUS_SUCCESS) {
            signals.hasContactCount = true;
            signals.contactCount = contactCount;
        }
    }

    const hid::Contact contact = hid::CombineContactSignals(signals);

    ULONG rawX = 0;
    ULONG rawY = 0;
    const bool hasPosition =
        HidP_GetUsageValue(HidP_Input, kUsagePageGenericDesktop, 0, kUsageX, &rawX,
                           device.preparsed, reportData, length) == HIDP_STATUS_SUCCESS &&
        HidP_GetUsageValue(HidP_Input, kUsagePageGenericDesktop, 0, kUsageY, &rawY,
                           device.preparsed, reportData, length) == HIDP_STATUS_SUCCESS;

    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    if (hasPosition) {
        normalizedX = hid::NormalizeValue(rawX, device.logicalXMin, device.logicalXMax,
                                          device.xBitSize);
        // HID digitizer Y grows downward; the gesture engine works in a
        // bottom-up space where the bottom of the pad is 0 and the top is 1.
        normalizedY =
            1.0f - hid::NormalizeValue(rawY, device.logicalYMin, device.logicalYMax,
                                       device.yBitSize);
    }

    // ------------------------------------------------------------------
    // Gesture lifecycle.
    // ------------------------------------------------------------------
    if (contact == hid::Contact::Multiple) {
        CancelActive();
        return;
    }

    if (contact == hid::Contact::Up) {
        if (activeDevice_ == deviceHandle) {
            activeDevice_ = nullptr;
            if (callbacks_.onContactUp) {
                callbacks_.onContactUp();
            }
        }
        device.blocked = false;
        return;
    }

    if (contact == hid::Contact::Unknown && !hasPosition) {
        // A digitizer without a tip switch signals release by omitting the
        // position entirely.
        if (!device.hasTipSwitch) {
            if (activeDevice_ == deviceHandle) {
                activeDevice_ = nullptr;
                if (callbacks_.onContactUp) {
                    callbacks_.onContactUp();
                }
            }
            device.blocked = false;
        }
        return;
    }

    if (!hasPosition) {
        // Tip is down but this report carries no coordinates (for example a
        // button-only report).  Wait for the next contact report.
        return;
    }

    // After a multi-finger report, wait for every contact to lift before
    // starting a new one-finger gesture.
    if (device.blocked) {
        return;
    }

    if (activeDevice_ != nullptr && activeDevice_ != deviceHandle) {
        return;
    }

    if (activeDevice_ == nullptr) {
        activeDevice_ = deviceHandle;
        if (callbacks_.onContactDown) {
            callbacks_.onContactDown(normalizedX, normalizedY);
        }
    } else if (callbacks_.onContactMove) {
        callbacks_.onContactMove(normalizedX, normalizedY);
    }
}

void RawInputSource::CancelActive() {
    if (activeDevice_ == nullptr) {
        return;
    }

    const auto found = devices_.find(activeDevice_);
    if (found != devices_.end()) {
        found->second->blocked = true;
    }
    activeDevice_ = nullptr;

    if (callbacks_.onCancel) {
        callbacks_.onCancel();
    }
}

} // namespace st
