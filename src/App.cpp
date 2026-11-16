#include "App.h"

#include "Actions.h"
#include "Install.h"
#include "Log.h"
#include "RawInput.h"
#include "Resources.h"
#include "Settings.h"
#include "Strings.h"
#include "TrayIcon.h"

#include <windows.h>
#include <shellapi.h>
#include <wtsapi32.h>

#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <utility>

namespace st {
namespace {

// Posted by the configuration watcher (worker thread) to the input window.
constexpr UINT kReloadConfigMessage = WM_APP + 60;
bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) {
        return false;
    }
    EmptyClipboard();

    bool copied = false;
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* destination = GlobalLock(memory);
        if (destination != nullptr) {
            std::memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (SetClipboardData(CF_UNICODETEXT, memory) != nullptr) {
                memory = nullptr; // owned by the clipboard now
                copied = true;
            }
        }
        if (memory != nullptr) {
            GlobalFree(memory);
        }
    }

    CloseClipboard();
    return copied;
}

const wchar_t* DevicesText(DigitizerDevices devices) {
    switch (devices) {
    case DigitizerDevices::Touchscreen:
        return L"touchscreen";
    case DigitizerDevices::Both:
        return L"both";
    case DigitizerDevices::Touchpad:
        break;
    }
    return L"touchpad";
}

const wchar_t* TargetsText(BrightnessTargets targets) {
    switch (targets) {
    case BrightnessTargets::Primary:
        return L"primary";
    case BrightnessTargets::Internal:
        return L"internal";
    case BrightnessTargets::All:
        break;
    }
    return L"all";
}

} // namespace

App::App() = default;

App::~App() {
    // The watcher posts to the input window, so it must stop first.
    configWatcher_.Stop();

    if (inputWindow_ != nullptr) {
        WTSUnRegisterSessionNotification(inputWindow_);
    }

    diagnostics_.reset();

    if (tray_) {
        tray_->Remove();
        tray_.reset();
    }

    if (rawInput_) {
        rawInput_->Unregister();
        rawInput_.reset();
    }

    if (inputWindow_ != nullptr) {
        DestroyWindow(inputWindow_);
        inputWindow_ = nullptr;
    }

    actions_.reset();
    gestures_.reset();

    LogMessage(LogLevel::Info, L"Smart Trackpad stopped.");
    ShutdownLogging();
}

bool App::Initialize(HINSTANCE instance) {
    instance_ = instance;

    config_ = LoadConfig(GetConfigPath());
    InitLogging(GetDataDirectory() / L"smarttrackpad.log", config_.debugLogging);

    LogMessage(LogLevel::Info, L"Smart Trackpad starting.");
    LogFormat(LogLevel::Info, L"Version: %ls", SMARTTRACKPAD_VERSION);
    LogFormat(LogLevel::Info, L"Configuration: %ls", config_.configPath.c_str());
    LogFormat(LogLevel::Info, L"Log file: %ls", LogPath().c_str());

    actions_ = std::make_unique<ActionDispatcher>();
    gestures_ = std::make_unique<GestureEngine>(
        config_.gestures,
        [this](int percent) {
            if (!enabled_ || suspended_ || !actions_) {
                return;
            }
            ++volumeUpdates_;
            volumePercent_ = percent;
            lastAction_ = L"volume " + std::to_wstring(percent) + L"%";
            actions_->SetVolumePercent(percent);
            UpdateDiagnostics();
        },
        [this](EdgeAction action) {
            if (!enabled_ || suspended_ || !actions_) {
                return;
            }
            switch (action) {
            case EdgeAction::MediaNext:
                ++mediaSteps_;
                lastAction_ = L"media next";
                break;
            case EdgeAction::MediaPrevious:
                ++mediaSteps_;
                lastAction_ = L"media previous";
                break;
            case EdgeAction::MediaPlayPause:
                ++tapCount_;
                lastAction_ = L"play / pause";
                break;
            case EdgeAction::ToggleMute:
                ++tapCount_;
                lastAction_ = L"mute toggle";
                break;
            case EdgeAction::MonitorOff:
                ++tapCount_;
                lastAction_ = L"display off";
                break;
            }
            actions_->Handle(action);
            UpdateDiagnostics();
        },
        [this](int percent) {
            if (!enabled_ || suspended_ || !actions_) {
                return;
            }
            ++brightnessUpdates_;
            brightnessPercent_ = percent;
            lastAction_ = L"brightness " + std::to_wstring(percent) + L"%";
            actions_->SetBrightnessPercent(percent);
            UpdateDiagnostics();
        });

    if (!CreateInputWindow()) {
        MessageBoxW(nullptr, L"Smart Trackpad could not create its input window.",
                    L"Smart Trackpad", MB_ICONERROR | MB_OK);
        return false;
    }

    RawInputSource::Callbacks callbacks;
    callbacks.onContactDown = [this](float x, float y) {
        hasContact_ = true;
        contactX_ = x;
        contactY_ = y;
        if (gestures_ && !suspended_) {
            gestures_->OnContactDown(x, y);
        }
        UpdateDiagnostics();
    };
    callbacks.onContactMove = [this](float x, float y) {
        hasContact_ = true;
        contactX_ = x;
        contactY_ = y;
        if (gestures_ && !suspended_) {
            gestures_->OnContactMove(x, y);
        }
        UpdateDiagnostics();
    };
    callbacks.onContactUp = [this] {
        hasContact_ = false;
        if (gestures_) {
            gestures_->OnContactUp();
        }
        UpdateDiagnostics();
    };
    callbacks.onCancel = [this] {
        hasContact_ = false;
        if (gestures_) {
            gestures_->OnContactCancel();
        }
        UpdateDiagnostics();
    };

    rawInput_ = std::make_unique<RawInputSource>(std::move(callbacks));

    if (!rawInput_->Register(inputWindow_, config_.devices)) {
        LogMessage(LogLevel::Error,
                   L"No HID digitizer collection could be registered.");
        const std::wstring text =
            L"Smart Trackpad could not register for touchpad input.\n\n"
            L"Details were written to:\n" +
            LogPath().wstring();
        MessageBoxW(nullptr, text.c_str(), L"Smart Trackpad",
                    MB_ICONERROR | MB_OK);
        return false;
    }

    ApplySettings();
    CreateTrayIcon();
    LogStartupSummary();

    // Locking the workstation should not leave a half-finished gesture behind,
    // and a locked session must not keep changing volume or brightness.
    if (!WTSRegisterSessionNotification(inputWindow_, NOTIFY_FOR_THIS_SESSION)) {
        LogFormat(LogLevel::Debug,
                  L"Session notifications unavailable (error %lu); gestures keep "
                  L"running while locked.",
                  GetLastError());
    }

    if (!configWatcher_.Watch(config_.configPath, [this] {
            if (inputWindow_ != nullptr) {
                PostMessageW(inputWindow_, kReloadConfigMessage, 0, 0);
            }
        })) {
        LogMessage(LogLevel::Warning,
                   L"Configuration changes cannot be watched; restart the "
                   L"application after editing the INI file.");
    }

    return true;
}

void App::SetStartupAction(StartupAction action) {
    startupAction_ = action;
}

int App::Run() {
    // Deferred until the loop exists: the settings dialog runs its own modal
    // loop and the zone test window needs the application loop to paint.
    if (startupAction_ == StartupAction::Settings) {
        PostMessageW(inputWindow_, WM_APP + 1, 0, 0);
    } else if (startupAction_ == StartupAction::ZoneTest) {
        PostMessageW(inputWindow_, WM_APP + 2, 0, 0);
    }

    MSG message{};
    BOOL result = FALSE;

    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return result == -1 ? 1 : static_cast<int>(message.wParam);
}

bool App::CreateInputWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &App::InputWndProcStatic;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kInputWindowClassName;

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogFormat(LogLevel::Error, L"RegisterClassEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    inputWindow_ = CreateWindowExW(0, kInputWindowClassName, L"Smart Trackpad Input", 0, 0,
                                   0, 0, 0, HWND_MESSAGE, nullptr, instance_, this);
    if (inputWindow_ == nullptr) {
        LogFormat(LogLevel::Error,
                  L"CreateWindowEx(HWND_MESSAGE) failed (error %lu).", GetLastError());
        return false;
    }

    LogMessage(LogLevel::Info, L"Message-only input window created.");
    return true;
}

void App::CreateTrayIcon() {
    tray_ = std::make_unique<TrayIcon>();

    TrayCallbacks callbacks;
    callbacks.onToggleEnabled = [this] { OnToggleEnabled(); };
    callbacks.onOpenSettings = [this] { OnOpenSettings(); };
    callbacks.onOpenZoneTest = [this] { OnOpenZoneTest(); };
    callbacks.onReloadConfig = [this] {
        if (inputWindow_ != nullptr) {
            PostMessageW(inputWindow_, kReloadConfigMessage, 0, 0);
        }
    };
    callbacks.onOpenConfig = [this] { OpenPath(config_.configPath); };
    callbacks.onOpenLog = [this] { OpenPath(LogPath()); };
    callbacks.onOpenDataFolder = [this] { OpenPath(GetDataDirectory()); };
    callbacks.onCopyLogPath = [this] { OnCopyLogPath(); };
    callbacks.onToggleStartWithWindows = [this] { OnToggleStartWithWindows(); };
    callbacks.onAbout = [this] { OnAbout(); };
    callbacks.onUninstall = [this] { OnUninstall(); };
    callbacks.onExit = [] {
        LogMessage(LogLevel::Info, L"Exit requested from the tray menu.");
        PostQuitMessage(0);
    };
    callbacks.onExternalCommand = [this](unsigned command) {
        if (command == kCommandOpenSettings) {
            OnOpenSettings();
        } else if (command == kCommandOpenZoneTest) {
            OnOpenZoneTest();
        } else if (command == kCommandExit) {
            PostQuitMessage(0);
        }
    };

    if (!tray_->Create(instance_, LoadString(IDS_APP_NAME), std::move(callbacks))) {
        LogMessage(LogLevel::Warning,
                   L"Tray icon unavailable; running without it. Use Task Manager "
                   L"or sign out to stop the process.");
        tray_.reset();
        return;
    }

    tray_->SetEnabled(enabled_);
    tray_->SetStartWithWindows(IsStartWithWindowsEnabled());
}

void App::ApplySettings() {
    if (gestures_) {
        gestures_->SetSettings(config_.gestures);
    }
    if (actions_) {
        actions_->SetBrightnessTargets(config_.brightnessTargets);
        actions_->SetOsdShowPercentage(config_.osdShowPercentage);
    }
}

void App::OnToggleEnabled() {
    enabled_ = !enabled_;
    if (gestures_) {
        gestures_->Reset();
    }
    if (tray_) {
        tray_->SetEnabled(enabled_);
    }
    LogFormat(LogLevel::Info, L"Edge gestures %ls.",
              enabled_ ? L"enabled" : L"disabled");
}

void App::OnToggleStartWithWindows() {
    const bool enable = !IsStartWithWindowsEnabled();
    if (!SetStartWithWindows(enable)) {
        LogMessage(LogLevel::Warning,
                   L"Could not change the Startup shortcut; see the log.");
        return;
    }

    config_.startWithWindows = enable;
    SaveConfig(config_.configPath, config_);
    if (tray_) {
        tray_->SetStartWithWindows(enable);
    }
    LogFormat(LogLevel::Info, L"Start with Windows %ls.",
              enable ? L"enabled" : L"disabled");
}

void App::OnOpenSettings() {
    LogMessage(LogLevel::Info, L"Settings dialog opened.");
    AppConfig edited = config_;
    if (!ShowSettingsDialog(instance_, nullptr, edited)) {
        return;
    }

    const bool devicesChanged = edited.devices != config_.devices;
    config_ = edited;

    if (!SaveConfig(config_.configPath, config_)) {
        MessageBoxW(nullptr, config_.configPath.c_str(), L"Smart Trackpad",
                    MB_ICONWARNING | MB_OK);
    }

    // The start-with-Windows preference is reflected on disk too.
    SetStartWithWindows(config_.startWithWindows);
    if (tray_) {
        tray_->SetStartWithWindows(config_.startWithWindows);
    }

    ApplySettings();

    if (devicesChanged && rawInput_) {
        rawInput_->Unregister();
        if (!rawInput_->Register(inputWindow_, config_.devices)) {
            LogMessage(LogLevel::Warning,
                       L"No digitizer is registered for the new input filter.");
        }
    }

    LogMessage(LogLevel::Info, L"Settings changed from the settings dialog.");
    LogStartupSummary();
}

void App::OnOpenZoneTest() {
    LogMessage(LogLevel::Info, L"Zone test window toggled.");
    if (!diagnostics_) {
        diagnostics_ = std::make_unique<DiagnosticsWindow>();
        if (!diagnostics_->Create(instance_)) {
            diagnostics_.reset();
            MessageBoxW(nullptr, L"The zone test window could not be created.",
                        L"Smart Trackpad", MB_ICONWARNING | MB_OK);
            return;
        }
    }

    if (diagnostics_->IsVisible()) {
        diagnostics_->Hide();
        return;
    }

    diagnostics_->Show();
    // Update after Show: UpdateDiagnostics() ignores hidden windows, so doing it
    // the other way round leaves the device line blank until the first contact.
    deviceSummary_ = rawInput_ ? rawInput_->DescribeDevices() : L"";
    UpdateDiagnostics();
}

void App::OnUninstall() {
    wchar_t confirm[1024]{};
    swprintf(confirm, std::size(confirm),
             LoadString(IDS_UNINSTALL_CONFIRM).c_str(), GetDataDirectory().c_str());

    if (MessageBoxW(nullptr, confirm, LoadString(IDS_APP_NAME).c_str(),
                    MB_ICONQUESTION | MB_YESNO) != IDYES) {
        return;
    }

    RemoveManagedInstall();
    if (IsStartWithWindowsEnabled()) {
        MessageBoxW(nullptr, LoadString(IDS_UNINSTALL_FAILED).c_str(),
                    LoadString(IDS_APP_NAME).c_str(), MB_ICONWARNING | MB_OK);
    }

    LogMessage(LogLevel::Info,
               L"Uninstall requested; the tray icon stays until the process exits.");
    PostQuitMessage(0);
}

void App::OnAbout() {
    wchar_t text[1024]{};
    swprintf(text, std::size(text), LoadString(IDS_ABOUT_FORMAT).c_str(),
             SMARTTRACKPAD_VERSION, config_.configPath.c_str(), LogPath().c_str());

    MessageBoxW(nullptr, text, LoadString(IDS_APP_NAME).c_str(),
                MB_ICONINFORMATION | MB_OK);
}

void App::OnCopyLogPath() {
    const bool copied = CopyTextToClipboard(inputWindow_, LogPath().wstring());
    MessageBoxW(nullptr,
                LoadString(copied ? IDS_COPY_DONE : IDS_COPY_FAILED).c_str(),
                LoadString(IDS_APP_NAME).c_str(), MB_ICONINFORMATION | MB_OK);
}

void App::OnReloadConfig() {
    const AppConfig updated = LoadConfig(config_.configPath);
    const bool devicesChanged = updated.devices != config_.devices;
    config_ = updated;

    InitLogging(LogPath(), config_.debugLogging);
    ApplySettings();

    if (devicesChanged && rawInput_) {
        rawInput_->Unregister();
        if (!rawInput_->Register(inputWindow_, config_.devices)) {
            LogMessage(LogLevel::Warning,
                       L"No digitizer is registered for the new input filter.");
        }
    }

    if (tray_) {
        tray_->SetStartWithWindows(config_.startWithWindows);
    }

    LogMessage(LogLevel::Info, L"Configuration reloaded from disk.");
    LogStartupSummary();
}

void App::OpenPath(const std::filesystem::path& path) const {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                           nullptr, SW_SHOWNORMAL);
    const INT_PTR code = reinterpret_cast<INT_PTR>(result);
    if (code <= 32) {
        LogFormat(LogLevel::Warning,
                  L"ShellExecute failed for \"%ls\" (code %lld).", path.c_str(),
                  static_cast<long long>(code));
        MessageBoxW(nullptr, path.c_str(), L"Smart Trackpad",
                    MB_ICONINFORMATION | MB_OK);
    }
}

void App::LogStartupSummary() const {
    const GestureSettings& gestures = config_.gestures;

    LogFormat(LogLevel::Info,
              L"Zones: left %ls %.0f%% (absolute volume), right %ls %.0f%% "
              L"(absolute brightness), top %ls %.0f%%; activation %.1f%%, "
              L"media step %.1f%%.",
              gestures.leftEnabled ? L"on" : L"off", gestures.leftBand * 100.0f,
              gestures.rightEnabled ? L"on" : L"off",
              gestures.rightBand * 100.0f, gestures.topEnabled ? L"on" : L"off",
              gestures.topBand * 100.0f, gestures.activationDistance * 100.0f,
              gestures.stepDistance * 100.0f);

    LogFormat(LogLevel::Info,
              L"Behaviour: taps %ls, cooldown %d ms, volume %d-%d%%, "
              L"brightness %d-%d%%, devices %ls, brightness targets %ls, overlay "
              L"percentage %ls, start with Windows %ls.",
              gestures.tapsEnabled ? L"on" : L"off", gestures.cooldownMs,
              gestures.minVolumePercent, gestures.maxVolumePercent,
              gestures.minBrightnessPercent, gestures.maxBrightnessPercent,
              DevicesText(config_.devices), TargetsText(config_.brightnessTargets),
              config_.osdShowPercentage ? L"on" : L"off",
              config_.startWithWindows ? L"on" : L"off");
}

void App::UpdateDiagnostics() {
    if (!diagnostics_ || !diagnostics_->IsVisible()) {
        return;
    }

    DiagnosticsSnapshot snapshot;
    snapshot.hasContact = hasContact_;
    snapshot.x = contactX_;
    snapshot.y = contactY_;
    snapshot.zone = gestures_ ? gestures_->ActiveZone() : EdgeZone::None;
    snapshot.deviceSummary = deviceSummary_;
    snapshot.lastAction = lastAction_;
    snapshot.volumePercent = volumePercent_;
    snapshot.brightnessPercent = brightnessPercent_;
    snapshot.volumeUpdates = volumeUpdates_;
    snapshot.brightnessUpdates = brightnessUpdates_;
    snapshot.mediaSteps = mediaSteps_;
    snapshot.taps = tapCount_;
    snapshot.settings = config_.gestures;
    diagnostics_->Update(snapshot);
}

void App::SetSuspended(bool suspended) {
    if (suspended_ == suspended) {
        return;
    }
    suspended_ = suspended;

    if (suspended_) {
        if (gestures_) {
            gestures_->Reset();
        }
        hasContact_ = false;
        LogMessage(LogLevel::Info,
                   L"Session locked; edge gestures are suspended.");
        UpdateDiagnostics();
    } else {
        LogMessage(LogLevel::Info, L"Session unlocked; edge gestures resumed.");
    }
}

LRESULT CALLBACK App::InputWndProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam) {
    App* self = nullptr;

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->inputWindow_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->InputWndProc(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::InputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INPUT:
        if (rawInput_) {
            rawInput_->HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
        }
        // DefWindowProc must run for WM_INPUT so raw input can be freed.
        break;

    case WM_INPUT_DEVICE_CHANGE:
        if (rawInput_) {
            rawInput_->HandleDeviceChange(wParam, reinterpret_cast<HANDLE>(lParam));
        }
        break;

    case kReloadConfigMessage:
        OnReloadConfig();
        break;

    case WM_APP + 1:
        OnOpenSettings();
        break;

    case WM_APP + 2:
        OnOpenZoneTest();
        break;

    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_LOGOFF) {
            SetSuspended(true);
        } else if (wParam == WTS_SESSION_UNLOCK) {
            SetSuspended(false);
        }
        break;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        if (inputWindow_ == hwnd) {
            inputWindow_ = nullptr;
        }
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace st
