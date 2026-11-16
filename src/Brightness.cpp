#include "Brightness.h"

#include "ComPtr.h"
#include "Log.h"

#include <windows.h>
#include <highlevelmonitorconfigurationapi.h>
#include <objbase.h>
#include <oleauto.h>
#include <physicalmonitorenumerationapi.h>
#include <wbemidl.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace st {
namespace {

constexpr unsigned long long kReenumerationDelayMs = 5000;

// BSTR RAII wrapper for the few fixed WMI strings used here.
class Bstr {
public:
    explicit Bstr(const wchar_t* text) : value_(SysAllocString(text)) {}
    explicit Bstr(const std::wstring& text)
        : value_(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
    ~Bstr() { SysFreeString(value_); }

    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    operator BSTR() const noexcept { return value_; }

private:
    BSTR value_ = nullptr;
};

} // namespace

struct BrightnessController::WmiState {
    bool attempted = false;
    bool available = false;
    ComPtr<IWbemServices> services;
    ComPtr<IWbemClassObject> methodClass;
    std::vector<std::wstring> instancePaths;

    bool Initialize();
    bool Apply(int percent);
};

bool BrightnessController::WmiState::Initialize() {
    if (attempted) {
        return available;
    }
    attempted = true;

    IWbemLocator* rawLocator = nullptr;
    HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IWbemLocator,
                                      reinterpret_cast<void**>(&rawLocator));
    ComPtr<IWbemLocator> locator;
    locator.Attach(rawLocator);
    if (FAILED(result) || !locator) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: CoCreateInstance(IWbemLocator) failed (0x%08lX).",
                  static_cast<unsigned long>(result));
        return false;
    }

    Bstr namespacePath(L"ROOT\\WMI");
    result = locator->ConnectServer(namespacePath, nullptr, nullptr, nullptr, 0, nullptr,
                                    nullptr, services.Put());
    if (FAILED(result)) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: WMI ConnectServer failed (0x%08lX).",
                  static_cast<unsigned long>(result));
        return false;
    }

    result = CoSetProxyBlanket(services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                               nullptr, RPC_C_AUTHN_LEVEL_CALL,
                               RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(result)) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: CoSetProxyBlanket failed (0x%08lX).",
                  static_cast<unsigned long>(result));
        return false;
    }

    // WmiMonitorBrightnessMethods exposes the WmiSetBrightness(uint32 Timeout,
    // uint8 Brightness) method.  Instance paths are stable for the session.
    Bstr queryLanguage(L"WQL");
    Bstr instanceQuery(L"SELECT __PATH FROM WmiMonitorBrightnessMethods");
    ComPtr<IEnumWbemClassObject> enumerator;
    result = services->ExecQuery(queryLanguage, instanceQuery,
                                 WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                 nullptr, enumerator.Put());
    if (FAILED(result)) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: WMI brightness query failed (0x%08lX).",
                  static_cast<unsigned long>(result));
        return false;
    }

    for (;;) {
        IWbemClassObject* rawInstance = nullptr;
        ULONG returned = 0;
        result = enumerator->Next(WBEM_INFINITE, 1, &rawInstance, &returned);
        if (result != S_OK || returned == 0) {
            break;
        }

        ComPtr<IWbemClassObject> instance;
        instance.Attach(rawInstance);

        VARIANT path;
        VariantInit(&path);
        if (SUCCEEDED(instance->Get(L"__PATH", 0, &path, nullptr, nullptr)) &&
            path.vt == VT_BSTR) {
            instancePaths.emplace_back(path.bstrVal);
        }
        VariantClear(&path);
    }

    if (instancePaths.empty()) {
        LogMessage(LogLevel::Debug,
                   L"Brightness: WMI reports no WmiMonitorBrightnessMethods instance.");
        return false;
    }

    Bstr className(L"WmiMonitorBrightnessMethods");
    result = services->GetObject(className, 0, nullptr, methodClass.Put(), nullptr);
    if (FAILED(result) || !methodClass) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: WMI GetObject(WmiMonitorBrightnessMethods) failed "
                  L"(0x%08lX).",
                  static_cast<unsigned long>(result));
        instancePaths.clear();
        return false;
    }

    available = true;
    LogFormat(LogLevel::Info,
              L"Brightness: WMI fallback available for %u internal display(s).",
              static_cast<unsigned>(instancePaths.size()));
    return true;
}

bool BrightnessController::WmiState::Apply(int percent) {
    if (!available) {
        return false;
    }

    const BYTE brightness = static_cast<BYTE>(std::clamp(percent, 0, 100));

    for (const std::wstring& path : instancePaths) {
        ComPtr<IWbemClassObject> inParams;
        HRESULT result =
            methodClass->GetMethod(L"WmiSetBrightness", 0, inParams.Put(), nullptr);
        if (FAILED(result)) {
            LogFormat(LogLevel::Warning,
                      L"Brightness: WMI GetMethod failed (0x%08lX).",
                      static_cast<unsigned long>(result));
            return false;
        }

        VARIANT timeout;
        VariantInit(&timeout);
        timeout.vt = VT_I4;
        timeout.lVal = 0;

        VARIANT level;
        VariantInit(&level);
        level.vt = VT_UI1;
        level.bVal = brightness;

        result = inParams->Put(L"Timeout", 0, &timeout, 0);
        if (SUCCEEDED(result)) {
            result = inParams->Put(L"Brightness", 0, &level, 0);
        }
        if (SUCCEEDED(result)) {
            Bstr instancePath(path);
            Bstr methodName(L"WmiSetBrightness");
            result = services->ExecMethod(instancePath, methodName, 0, nullptr,
                                          inParams.Get(), nullptr, nullptr);
        }

        VariantClear(&timeout);
        VariantClear(&level);

        if (FAILED(result)) {
            LogFormat(LogLevel::Warning,
                      L"Brightness: WMI WmiSetBrightness failed (0x%08lX).",
                      static_cast<unsigned long>(result));
            return false;
        }
    }

    return true;
}

BrightnessController::BrightnessController(LevelFeedbackFn onApplied)
    : onApplied_(std::move(onApplied)) {
    worker_ = std::jthread([this](std::stop_token stop) { Run(std::move(stop)); });
}

BrightnessController::~BrightnessController() {
    worker_.request_stop();
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    Release();
}

void BrightnessController::SetTargetPercent(int percent) {
    percent = std::clamp(percent, 0, 100);

    {
        std::lock_guard lock(mutex_);
        pendingPercent_ = percent;
    }
    condition_.notify_one();
}

void BrightnessController::Run(std::stop_token stop) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    comReady_ = SUCCEEDED(comResult);

    if (comReady_) {
        const HRESULT security = CoInitializeSecurity(
            nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
        if (FAILED(security) && security != RPC_E_TOO_LATE) {
            LogFormat(LogLevel::Debug,
                      L"Brightness: CoInitializeSecurity failed (0x%08lX).",
                      static_cast<unsigned long>(security));
        }
    }

    {
        std::unique_lock lock(mutex_);

        for (;;) {
            condition_.wait(lock, [this, &stop] {
                return pendingPercent_ >= 0 || stop.stop_requested();
            });

            if (stop.stop_requested()) {
                break;
            }

            // Coalesce a fast slide into one monitor update.
            condition_.wait_for(lock, std::chrono::milliseconds(25));
            if (stop.stop_requested()) {
                break;
            }

            const int percent = std::exchange(pendingPercent_, -1);
            if (percent < 0) {
                continue;
            }

            lock.unlock();
            const std::optional<LevelFeedback> feedback = Apply(percent);
            if (feedback && onApplied_) {
                onApplied_(*feedback);
            }
            lock.lock();
        }
    }

    // Release WMI proxies on the thread that created them, while COM is still
    // initialized.
    wmi_.reset();
    if (comReady_) {
        CoUninitialize();
    }
}

std::optional<LevelFeedback> BrightnessController::Apply(int percent) {
    percent = std::clamp(percent, 0, 100);

    // (Re)discover DDC/CI monitors only while none are known; an established
    // list is refreshed when a monitor fails or the worker has no targets.
    if (monitors_.empty()) {
        const unsigned long long now = GetTickCount64();
        if (!enumerated_ || now - lastEnumerationTick_ > kReenumerationDelayMs) {
            enumerated_ = true;
            lastEnumerationTick_ = now;
            Enumerate();
        }
    }

    int effective = percent;
    bool anyApplied = false;

    if (!monitors_.empty() && ApplyDdc(percent, effective)) {
        anyApplied = true;
    }

    // Internal laptop panels usually reject DDC/CI, so the WMI value is applied
    // in addition to the DDC/CI monitors instead of only as a fallback.  On a
    // desktop without an internal panel WMI simply reports unavailability.
    if (ApplyWmi(percent)) {
        anyApplied = true;
        // The internal panel is what a laptop user is looking at, so report it
        // as the visible level.
        effective = percent;
    }

    if (!anyApplied) {
        if (!backendWarningLogged_) {
            backendWarningLogged_ = true;
            LogMessage(LogLevel::Warning,
                       L"Brightness: neither DDC/CI nor WMI accepted the value; "
                       L"the right edge has no visible effect.");
        }
        return std::nullopt;
    }

    backendWarningLogged_ = false;

    LevelFeedback feedback;
    feedback.percent = std::clamp(effective, 0, 100);
    return feedback;
}

bool BrightnessController::ApplyDdc(int percent, int& effectivePercent) {
    bool anyApplied = false;

    for (auto it = monitors_.begin(); it != monitors_.end();) {
        const long range = static_cast<long>(it->maximum) -
                           static_cast<long>(it->minimum);
        const long target =
            static_cast<long>(it->minimum) + (range * percent + 50) / 100;

        if (target == static_cast<long>(it->current)) {
            anyApplied = true;
            ++it;
            continue;
        }

        if (SetMonitorBrightness(it->handle, static_cast<DWORD>(target))) {
            it->current = static_cast<DWORD>(target);
            anyApplied = true;
            ++it;
        } else {
            LogFormat(LogLevel::Warning,
                      L"Brightness: SetMonitorBrightness failed for \"%ls\" "
                      L"(error %lu); dropping the monitor.",
                      it->description.c_str(), GetLastError());
            DestroyPhysicalMonitor(it->handle);
            it = monitors_.erase(it);
        }
    }

    // Report the level the first remaining DDC/CI monitor actually holds, in
    // case it quantizes the 0-100 value onto its own VCP range.
    if (!monitors_.empty()) {
        const Monitor& monitor = monitors_.front();
        const long range = static_cast<long>(monitor.maximum) -
                           static_cast<long>(monitor.minimum);
        if (range > 0) {
            const long offset = static_cast<long>(monitor.current) -
                                static_cast<long>(monitor.minimum);
            effectivePercent = std::clamp(
                static_cast<int>((offset * 100 + range / 2) / range), 0, 100);
        }
    }

    return anyApplied;
}

bool BrightnessController::ApplyWmi(int percent) {
    if (!comReady_) {
        return false;
    }

    if (!wmi_) {
        wmi_ = std::make_unique<WmiState>();
    }
    if (!wmi_->attempted) {
        wmi_->Initialize();
    }
    if (!wmi_->available) {
        return false;
    }

    return wmi_->Apply(percent);
}

void BrightnessController::Enumerate() {
    Release();

    EnumDisplayMonitors(nullptr, nullptr, &BrightnessController::EnumMonitorProc,
                        reinterpret_cast<LPARAM>(this));

    if (monitors_.empty()) {
        LogMessage(LogLevel::Debug,
                   L"Brightness: no DDC/CI monitor; using WMI for internal panels.");
    } else {
        LogFormat(LogLevel::Info, L"Brightness: %u monitor(s) support DDC/CI.",
                  static_cast<unsigned>(monitors_.size()));
    }
}

void BrightnessController::Release() {
    for (Monitor& monitor : monitors_) {
        if (monitor.handle != nullptr) {
            DestroyPhysicalMonitor(monitor.handle);
            monitor.handle = nullptr;
        }
    }
    monitors_.clear();
}

BOOL CALLBACK BrightnessController::EnumMonitorProc(HMONITOR monitor, HDC, LPRECT,
                                                    LPARAM context) {
    auto* self = reinterpret_cast<BrightnessController*>(context);

    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0) {
        return TRUE;
    }

    std::vector<PHYSICAL_MONITOR> physical(count);
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data())) {
        LogFormat(LogLevel::Debug,
                  L"Brightness: GetPhysicalMonitorsFromHMONITOR failed (error %lu).",
                  GetLastError());
        return TRUE;
    }

    for (const PHYSICAL_MONITOR& entry : physical) {
        DWORD minimum = 0;
        DWORD current = 0;
        DWORD maximum = 0;

        if (GetMonitorBrightness(entry.hPhysicalMonitor, &minimum, &current, &maximum) &&
            minimum < maximum) {
            Monitor state;
            state.handle = entry.hPhysicalMonitor;
            state.minimum = minimum;
            state.current = current;
            state.maximum = maximum;
            state.description = entry.szPhysicalMonitorDescription;
            self->monitors_.push_back(std::move(state));

            LogFormat(LogLevel::Debug,
                      L"Brightness: \"%ls\" range %lu..%lu (current %lu).",
                      entry.szPhysicalMonitorDescription, minimum, maximum, current);
        } else {
            DestroyPhysicalMonitor(entry.hPhysicalMonitor);
            LogFormat(LogLevel::Debug,
                      L"Brightness: \"%ls\" does not support DDC/CI brightness.",
                      entry.szPhysicalMonitorDescription);
        }
    }

    return TRUE;
}

} // namespace st
