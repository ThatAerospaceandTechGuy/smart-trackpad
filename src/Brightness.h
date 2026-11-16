#pragma once

#include <windows.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace st {

// Applies absolute display brightness without blocking the message loop.
// DDC/CI is used when a monitor exposes it; otherwise a WMI fallback handles
// internal laptop panels (WmiMonitorBrightnessMethods).
class BrightnessController {
public:
    BrightnessController();
    ~BrightnessController();

    BrightnessController(const BrightnessController&) = delete;
    BrightnessController& operator=(const BrightnessController&) = delete;

    // 0 = darkest, 100 = brightest.  Non-blocking; only the newest value is
    // applied, and bursts are coalesced on the worker thread.
    void SetTargetPercent(int percent);

private:
    struct Monitor {
        HANDLE handle = nullptr;
        DWORD minimum = 0;
        DWORD current = 0;
        DWORD maximum = 100;
        std::wstring description;
    };

    // COM/WMI state lives only on the brightness worker.  Released before the
    // worker uninitializes COM.
    struct WmiState;

    static BOOL CALLBACK EnumMonitorProc(HMONITOR monitor, HDC dc, LPRECT rect,
                                        LPARAM context);

    void Run(std::stop_token stop);
    void Enumerate();
    void Release();
    void Apply(int percent);
    void ApplyWmi(int percent);

    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    int pendingPercent_ = -1;

    bool comReady_ = false;
    bool wmiWarned_ = false;
    bool enumerated_ = false;
    unsigned long long lastEnumerationTick_ = 0;

    std::vector<Monitor> monitors_;
    std::unique_ptr<WmiState> wmi_;
};

} // namespace st
