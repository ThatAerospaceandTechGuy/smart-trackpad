#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Feedback.h"
#include "SettingsTypes.h"

namespace st {

// Applies absolute display brightness without blocking the message loop.
// DDC/CI is used for every MCCS-capable monitor and the WMI provider
// (WmiMonitorBrightnessMethods) additionally drives an internal laptop panel,
// so a laptop with an external screen attached moves both at once.
class BrightnessController {
public:
    explicit BrightnessController(LevelFeedbackFn onApplied = {});
    ~BrightnessController();

    BrightnessController(const BrightnessController&) = delete;
    BrightnessController& operator=(const BrightnessController&) = delete;

    // 0 = darkest, 100 = brightest.  Non-blocking; only the newest value is
    // applied, and bursts are coalesced on the worker thread.
    void SetTargetPercent(int percent);

    // Which displays the edge drives.  Safe to call from the UI thread while
    // the worker is applying a value.
    void SetTargets(BrightnessTargets targets);

private:
    struct Monitor {
        HANDLE handle = nullptr;
        DWORD minimum = 0;
        DWORD current = 0;
        DWORD maximum = 100;
        bool isPrimary = false;
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

    // Applies the value to every backend and returns the level the user can
    // actually see, or nullopt when no backend accepted it.
    std::optional<LevelFeedback> Apply(int percent);
    bool ApplyDdc(int percent, BrightnessTargets targets, int& effectivePercent);
    bool ApplyWmi(int percent);

    LevelFeedbackFn onApplied_;

    std::jthread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    int pendingPercent_ = -1;
    std::atomic<BrightnessTargets> targets_{BrightnessTargets::All};
    HMONITOR primaryMonitor_ = nullptr;

    bool comReady_ = false;
    bool backendWarningLogged_ = false;
    bool enumerated_ = false;
    unsigned long long lastEnumerationTick_ = 0;

    std::vector<Monitor> monitors_;
    std::unique_ptr<WmiState> wmi_;
};

} // namespace st
