#include "TrayIcon.h"

#include "Log.h"
#include "Resources.h"
#include "Strings.h"
#include "WindowNames.h"

#include <shellapi.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace st {
namespace {

constexpr const wchar_t* kClassName = kControlWindowClassName;

} // namespace

TrayIcon::TrayIcon() = default;

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Create(HINSTANCE instance, const std::wstring& title,
                      TrayCallbacks callbacks) {
    instance_ = instance;
    title_ = title;
    callbacks_ = std::move(callbacks);
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    commandMessage_ = RegisterWindowMessageW(kCommandMessageName);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayIcon::WndProcStatic;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogFormat(LogLevel::Warning, L"Tray: RegisterClassEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    // A hidden top-level window is used for the notification icon.  The raw
    // input window is message-only, as required by the input design.
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, title_.c_str(), WS_POPUP, 0,
                            0, 0, 0, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        LogFormat(LogLevel::Warning, L"Tray: CreateWindowEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    // Prefer the application icon embedded by Resources.rc; fall back to the
    // generic one only if the resource is missing (for example in a test
    // build that does not link the resource script).
    icon_ = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (icon_ == nullptr) {
        icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }

    BuildTooltip();
    AddOrUpdateIcon(true);
    return true;
}

void TrayIcon::SetEnabled(bool enabled) {
    enabled_ = enabled;
    BuildTooltip();
    if (hwnd_ != nullptr) {
        AddOrUpdateIcon(false);
    }
}

void TrayIcon::SetBrightnessEnabled(bool enabled) {
    brightnessEnabled_ = enabled;
}

void TrayIcon::SetStartWithWindows(bool enabled) {
    startWithWindows_ = enabled;
}

void TrayIcon::Remove() {
    if (added_ && hwnd_ != nullptr) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = hwnd_;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
        added_ = false;
    }

    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void TrayIcon::BuildTooltip() {
    tooltip_ = title_ + L" - " +
               LoadString(enabled_ ? IDS_TRAY_ENABLED : IDS_TRAY_DISABLED);
}

void TrayIcon::AddOrUpdateIcon(bool add) {
    if (hwnd_ == nullptr) {
        return;
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd_;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kCallbackMessage;
    data.hIcon = icon_;

    const std::size_t maxTipLength = std::size(data.szTip) - 1;
    const std::size_t tipLength = std::min(tooltip_.size(), maxTipLength);
    std::copy_n(tooltip_.data(), tipLength, data.szTip);
    data.szTip[tipLength] = L'\0';

    const BOOL succeeded = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);

    if (succeeded) {
        added_ = true;
        if (add) {
            LogFormat(LogLevel::Info, L"Tray: icon added (window 0x%p).",
                      reinterpret_cast<void*>(hwnd_));
        }
        return;
    }

    // Explorer may have restarted since the icon was added.  One retry with
    // NIM_ADD is enough to recover; NIM_MODIFY fails until the icon exists.
    if (!add) {
        added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (!added_) {
            LogFormat(LogLevel::Warning, L"Tray: Shell_NotifyIcon failed (error %lu).",
                      GetLastError());
        }
    } else {
        added_ = false;
        LogFormat(LogLevel::Warning, L"Tray: Shell_NotifyIcon failed (error %lu).",
                  GetLastError());
    }
}

void TrayIcon::ShowMenu() {
    POINT cursor{};
    GetCursorPos(&cursor);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    auto item = [menu](UINT flags, UINT_PTR id, unsigned stringId) {
        AppendMenuW(menu, flags, id, LoadString(stringId).c_str());
    };

    item(MF_STRING | (enabled_ ? MF_CHECKED : MF_UNCHECKED), kToggleCommand,
         IDS_MENU_EDGE_GESTURES);
    item(MF_STRING | (brightnessEnabled_ ? MF_CHECKED : MF_UNCHECKED),
         kBrightnessCommand, IDS_MENU_BRIGHTNESS_GESTURES);
    item(MF_STRING, kSettingsCommand, IDS_MENU_SETTINGS);
    item(MF_STRING, kZoneTestCommand, IDS_MENU_ZONE_TEST);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    item(MF_STRING, kReloadCommand, IDS_MENU_RELOAD);
    item(MF_STRING, kOpenConfigCommand, IDS_MENU_OPEN_CONFIG);
    item(MF_STRING, kOpenLogCommand, IDS_MENU_OPEN_LOG);
    item(MF_STRING, kCopyLogCommand, IDS_MENU_COPY_LOG_PATH);
    item(MF_STRING, kOpenDataCommand, IDS_MENU_OPEN_DATA_FOLDER);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    item(MF_STRING | (startWithWindows_ ? MF_CHECKED : MF_UNCHECKED), kStartupCommand,
         IDS_MENU_START_WITH_WINDOWS);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    item(MF_STRING, kAboutCommand, IDS_MENU_ABOUT);
    item(MF_STRING, kUninstallCommand, IDS_MENU_UNINSTALL);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    item(MF_STRING, kExitCommand, IDS_MENU_EXIT);

    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
        hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    auto invoke = [this](const std::function<void()>& callback) {
        if (callback) {
            callback();
        }
    };

    switch (command) {
    case kToggleCommand:
        invoke(callbacks_.onToggleEnabled);
        break;
    case kBrightnessCommand:
        invoke(callbacks_.onToggleBrightness);
        break;
    case kSettingsCommand:
        invoke(callbacks_.onOpenSettings);
        break;
    case kZoneTestCommand:
        invoke(callbacks_.onOpenZoneTest);
        break;
    case kReloadCommand:
        invoke(callbacks_.onReloadConfig);
        break;
    case kOpenConfigCommand:
        invoke(callbacks_.onOpenConfig);
        break;
    case kOpenLogCommand:
        invoke(callbacks_.onOpenLog);
        break;
    case kCopyLogCommand:
        invoke(callbacks_.onCopyLogPath);
        break;
    case kOpenDataCommand:
        invoke(callbacks_.onOpenDataFolder);
        break;
    case kStartupCommand:
        invoke(callbacks_.onToggleStartWithWindows);
        break;
    case kAboutCommand:
        invoke(callbacks_.onAbout);
        break;
    case kUninstallCommand:
        invoke(callbacks_.onUninstall);
        break;
    case kExitCommand:
        invoke(callbacks_.onExit);
        break;
    default:
        break;
    }
}

LRESULT CALLBACK TrayIcon::WndProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                         LPARAM lParam) {
    TrayIcon* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<TrayIcon*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->WndProc(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TrayIcon::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated_ != 0 && message == taskbarCreated_) {
        added_ = false;
        AddOrUpdateIcon(true);
        return 0;
    }

    if (commandMessage_ != 0 && message == commandMessage_) {
        if (callbacks_.onExternalCommand) {
            callbacks_.onExternalCommand(static_cast<unsigned>(wParam));
        }
        return 0;
    }

    if (message == kCallbackMessage) {
        switch (LOWORD(lParam)) {
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowMenu();
            break;
        default:
            break;
        }
        return 0;
    }

    (void)wParam;
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace st
