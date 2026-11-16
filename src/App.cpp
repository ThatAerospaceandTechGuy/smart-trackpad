#include "App.h"

#include "Actions.h"
#include "Install.h"
#include "Log.h"
#include "RawInput.h"
#include "TrayIcon.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <utility>

namespace st {
namespace {

constexpr wchar_t kInputClassName[] = L"SmartTrackpadInputWindow";

} // namespace

App::App() = default;

App::~App() {
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
    LogFormat(LogLevel::Info, L"Configuration: %ls", config_.configPath.c_str());
    LogFormat(LogLevel::Info, L"Log file: %ls", LogPath().c_str());

    EnsureInstalled();

    actions_ = std::make_unique<ActionDispatcher>();
    gestures_ = std::make_unique<GestureEngine>(
        config_.gestures,
        [this](int percent) {
            if (enabled_ && actions_) {
                actions_->SetVolumePercent(percent);
            }
        },
        [this](EdgeAction action) {
            if (enabled_ && actions_) {
                actions_->Handle(action);
            }
        },
        [this](int percent) {
            if (enabled_ && actions_) {
                actions_->SetBrightnessPercent(percent);
            }
        });

    if (!CreateInputWindow()) {
        MessageBoxW(nullptr, L"Smart Trackpad could not create its input window.",
                    L"Smart Trackpad", MB_ICONERROR | MB_OK);
        return false;
    }

    RawInputSource::Callbacks callbacks;
    callbacks.onContactDown = [this](float x, float y) {
        if (gestures_) {
            gestures_->OnContactDown(x, y);
        }
    };
    callbacks.onContactMove = [this](float x, float y) {
        if (gestures_) {
            gestures_->OnContactMove(x, y);
        }
    };
    callbacks.onContactUp = [this] {
        if (gestures_) {
            gestures_->OnContactUp();
        }
    };
    callbacks.onCancel = [this] {
        if (gestures_) {
            gestures_->OnContactCancel();
        }
    };

    rawInput_ = std::make_unique<RawInputSource>(std::move(callbacks));

    if (!rawInput_->Register(inputWindow_)) {
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

    CreateTrayIcon();
    LogStartupSummary();
    return true;
}

int App::Run() {
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
    windowClass.lpszClassName = kInputClassName;

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogFormat(LogLevel::Error, L"RegisterClassEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    inputWindow_ = CreateWindowExW(0, kInputClassName, L"Smart Trackpad Input", 0, 0,
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
    callbacks.onOpenConfig = [this] { OpenPath(config_.configPath); };
    callbacks.onOpenLog = [this] { OpenPath(LogPath()); };
    callbacks.onExit = [] {
        LogMessage(LogLevel::Info, L"Exit requested from the tray menu.");
        PostQuitMessage(0);
    };

    if (!tray_->Create(instance_, L"Smart Trackpad", std::move(callbacks))) {
        LogMessage(LogLevel::Warning,
                   L"Tray icon unavailable; running without it. Use Task Manager "
                   L"or sign out to stop the process.");
        tray_.reset();
        return;
    }

    tray_->SetEnabled(enabled_);
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
