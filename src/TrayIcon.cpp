#include "TrayIcon.h"

#include "Log.h"

#include <shellapi.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace st {
namespace {

constexpr wchar_t kClassName[] = L"SmartTrackpadTrayWindow";

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

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayIcon::WndProcStatic;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;

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

    icon_ = LoadIconW(nullptr, IDI_APPLICATION);
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

void TrayIcon::UpdateTooltip(const std::wstring& title) {
    title_ = title;
    BuildTooltip();
    if (hwnd_ != nullptr) {
        AddOrUpdateIcon(false);
    }
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
    tooltip_ = title_ + (enabled_ ? L" - enabled" : L" - disabled");
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
        return;
    }

    // Explorer may have restarted since the icon was added.  One retry with
    // NIM_ADD is enough to recover; NIM_MODIFY fails until the icon exists.
    if (!add) {
        added_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
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

    AppendMenuW(menu, MF_STRING | (enabled_ ? MF_CHECKED : MF_UNCHECKED),
                kToggleCommand, L"Edge gestures");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kOpenConfigCommand, L"Open configuration...");
    AppendMenuW(menu, MF_STRING, kOpenLogCommand, L"Open log...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kExitCommand, L"Exit");

    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0,
        hwnd_, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    switch (command) {
    case kToggleCommand:
        if (callbacks_.onToggleEnabled) {
            callbacks_.onToggleEnabled();
        }
        break;
    case kOpenConfigCommand:
        if (callbacks_.onOpenConfig) {
            callbacks_.onOpenConfig();
        }
        break;
    case kOpenLogCommand:
        if (callbacks_.onOpenLog) {
            callbacks_.onOpenLog();
        }
        break;
    case kExitCommand:
        if (callbacks_.onExit) {
            callbacks_.onExit();
        }
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
