#include "Diagnostics.h"

#include "Log.h"
#include "Resources.h"
#include "Strings.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <iterator>
#include <string>

namespace st {
namespace {

constexpr wchar_t kClassName[] = L"SmartTrackpadDiagnosticsWindow";
constexpr int kWindowWidth = 620;
constexpr int kWindowHeight = 470;

// Typical Windows Precision Touchpad surface ratio (width / height).
constexpr float kPadAspect = 1.45f;

COLORREF Blend(COLORREF background, COLORREF foreground, int weightPercent) {
    const int inverse = 100 - weightPercent;
    return RGB((GetRValue(background) * inverse + GetRValue(foreground) * weightPercent) / 100,
               (GetGValue(background) * inverse + GetGValue(foreground) * weightPercent) / 100,
               (GetBValue(background) * inverse + GetBValue(foreground) * weightPercent) / 100);
}

} // namespace

DiagnosticsWindow::DiagnosticsWindow() = default;

DiagnosticsWindow::~DiagnosticsWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool DiagnosticsWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    if (hwnd_ != nullptr) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &DiagnosticsWindow::WindowProcStatic;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));

    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogFormat(LogLevel::Warning,
                  L"Zone test: RegisterClassEx failed (error %lu).", GetLastError());
        return false;
    }

    RECT windowRect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    hwnd_ = CreateWindowExW(
        WS_EX_TOOLWINDOW, kClassName, LoadString(IDS_ZONE_TEST_TITLE).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top, nullptr, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        LogFormat(LogLevel::Warning, L"Zone test: CreateWindowEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    // Keep the window inside the work area of the monitor it opens on.
    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        const int left = info.rcWork.left + 40;
        const int top = info.rcWork.top + 40;
        SetWindowPos(hwnd_, nullptr, left, top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    return true;
}

void DiagnosticsWindow::Show() {
    if (hwnd_ == nullptr) {
        return;
    }
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void DiagnosticsWindow::Hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool DiagnosticsWindow::IsVisible() const {
    return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

void DiagnosticsWindow::Update(const DiagnosticsSnapshot& snapshot) {
    snapshot_ = snapshot;
    if (hwnd_ != nullptr && IsVisible()) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void DiagnosticsWindow::Paint(HDC target, const RECT& client) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const HDC memory = CreateCompatibleDC(target);
    const HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
    const HGDIOBJ previousBitmap = SelectObject(memory, bitmap);

    const COLORREF background = GetSysColor(COLOR_WINDOW);
    const COLORREF foreground = GetSysColor(COLOR_WINDOWTEXT);
    const COLORREF accent = GetSysColor(COLOR_HIGHLIGHT);

    const HBRUSH backgroundBrush = CreateSolidBrush(background);
    FillRect(memory, &client, backgroundBrush);
    DeleteObject(backgroundBrush);

    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, foreground);

    const int dpi = hwnd_ != nullptr ? static_cast<int>(GetDpiForWindow(hwnd_)) : 96;
    const int scale = std::max(1, MulDiv(dpi, 1, 96));

    const HFONT titleFont = CreateFontW(-MulDiv(15, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE,
                                        FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const HFONT bodyFont = CreateFontW(-MulDiv(12, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE,
                                       FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH, L"Segoe UI");

    const int margin = 20 * scale;
    int y = margin;

    HGDIOBJ previousFont = SelectObject(memory, titleFont);
    const std::wstring title = LoadString(IDS_ZONE_TEST_TITLE);
    TextOutW(memory, margin, y, title.c_str(), static_cast<int>(title.size()));
    y += MulDiv(26, dpi, 96);

    SelectObject(memory, bodyFont);
    const std::wstring help = LoadString(IDS_ZONE_TEST_HELP);
    TextOutW(memory, margin, y, help.c_str(), static_cast<int>(help.size()));
    y += MulDiv(22, dpi, 96);

    // --- pad surface -------------------------------------------------------
    const int availableWidth = width - margin * 2;
    const int availableHeight = height - y - MulDiv(110, dpi, 96);
    int padWidth = std::min(availableWidth, MulDiv(420, dpi, 96));
    int padHeight = static_cast<int>(static_cast<float>(padWidth) / kPadAspect);
    if (padHeight > availableHeight && availableHeight > 0) {
        padHeight = availableHeight;
        padWidth = static_cast<int>(static_cast<float>(padHeight) * kPadAspect);
    }
    if (padWidth < 40 || padHeight < 40) {
        SelectObject(memory, previousFont);
        DeleteObject(titleFont);
        DeleteObject(bodyFont);
        SelectObject(memory, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(memory);
        return;
    }

    const RECT pad{margin, y, margin + padWidth, y + padHeight};

    const HBRUSH padBrush = CreateSolidBrush(Blend(background, foreground, 12));
    FillRect(memory, &pad, padBrush);
    DeleteObject(padBrush);

    const GestureSettings& settings = snapshot_.settings;
    auto bandRect = [&](float from, float to, bool vertical) {
        RECT rect = pad;
        if (vertical) {
            rect.left = pad.left + static_cast<int>(static_cast<float>(padWidth) * from);
            rect.right =
                pad.left + static_cast<int>(static_cast<float>(padWidth) * to);
        } else {
            // Top band: y is bottom-up, so [1-band, 1] is the top of the pad.
            rect.top = pad.bottom - static_cast<int>(static_cast<float>(padHeight) * to);
            rect.bottom =
                pad.bottom - static_cast<int>(static_cast<float>(padHeight) * from);
        }
        return rect;
    };

    auto fillBand = [&](const RECT& rect, EdgeZone zone, bool enabled) {
        if (!enabled) {
            return;
        }
        const COLORREF colour =
            snapshot_.zone == zone ? accent : Blend(background, accent, 35);
        const HBRUSH brush = CreateSolidBrush(colour);
        FillRect(memory, &rect, brush);
        DeleteObject(brush);
    };

    fillBand(bandRect(0.0f, settings.leftBand, true), EdgeZone::Left,
             settings.leftEnabled);
    fillBand(bandRect(1.0f - settings.rightBand, 1.0f, true), EdgeZone::Right,
             settings.rightEnabled);
    fillBand(bandRect(1.0f - settings.topBand, 1.0f, false), EdgeZone::Top,
             settings.topEnabled);

    // Contact, mapped from the bottom-up coordinate space.
    if (snapshot_.hasContact) {
        const int px = pad.left + static_cast<int>(
                                     static_cast<float>(padWidth) *
                                     std::clamp(snapshot_.x, 0.0f, 1.0f));
        const int py = pad.bottom - static_cast<int>(
                                        static_cast<float>(padHeight) *
                                        std::clamp(snapshot_.y, 0.0f, 1.0f));
        const int radius = 7 * scale;

        const HBRUSH dotBrush = CreateSolidBrush(accent);
        const HPEN dotPen = CreatePen(PS_SOLID, 2 * scale, foreground);
        const HGDIOBJ oldBrush = SelectObject(memory, dotBrush);
        const HGDIOBJ oldPen = SelectObject(memory, dotPen);
        Ellipse(memory, px - radius, py - radius, px + radius, py + radius);
        SelectObject(memory, oldPen);
        SelectObject(memory, oldBrush);
        DeleteObject(dotPen);
        DeleteObject(dotBrush);
    }

    const HPEN framePen = CreatePen(PS_SOLID, scale, foreground);
    const HGDIOBJ oldPen = SelectObject(memory, framePen);
    const HGDIOBJ oldBrush = SelectObject(memory, GetStockObject(NULL_BRUSH));
    Rectangle(memory, pad.left, pad.top, pad.right, pad.bottom);
    SelectObject(memory, oldBrush);
    SelectObject(memory, oldPen);
    DeleteObject(framePen);

    // Band captions.
    y = pad.bottom + MulDiv(6, dpi, 96);
    const std::wstring leftCaption = LoadString(IDS_ZONE_TEST_LEFT);
    const std::wstring rightCaption = LoadString(IDS_ZONE_TEST_RIGHT);
    const std::wstring topCaption = LoadString(IDS_ZONE_TEST_TOP);
    TextOutW(memory, pad.left, y, leftCaption.c_str(),
             static_cast<int>(leftCaption.size()));
    TextOutW(memory, pad.right - MulDiv(120, dpi, 96), y, rightCaption.c_str(),
             static_cast<int>(rightCaption.size()));
    TextOutW(memory, pad.left + MulDiv(140, dpi, 96), y, topCaption.c_str(),
             static_cast<int>(topCaption.size()));
    y += MulDiv(24, dpi, 96);

    SetTextColor(memory, Blend(background, foreground, 55));
    const std::wstring devices = snapshot_.deviceSummary;
    TextOutW(memory, margin, y, devices.c_str(), static_cast<int>(devices.size()));
    y += MulDiv(20, dpi, 96);

    wchar_t contactLine[128]{};
    if (snapshot_.hasContact) {
        swprintf(contactLine, std::size(contactLine),
                 LoadString(IDS_ZONE_TEST_CONTACT).c_str(),
                 static_cast<double>(snapshot_.x), static_cast<double>(snapshot_.y));
    } else {
        swprintf(contactLine, std::size(contactLine), L"%s",
                 LoadString(IDS_ZONE_TEST_NO_CONTACT).c_str());
    }
    TextOutW(memory, margin, y, contactLine, static_cast<int>(wcslen(contactLine)));

    SetTextColor(memory, foreground);
    const std::wstring lastAction =
        snapshot_.lastAction.empty() ? std::wstring(L"no action yet")
                                     : snapshot_.lastAction;
    const int actionWidth = width - margin * 2 - MulDiv(190, dpi, 96);
    TextOutW(memory, margin + MulDiv(190, dpi, 96), y, lastAction.c_str(),
             std::min(static_cast<int>(lastAction.size()), std::max(actionWidth, 0)));

    SelectObject(memory, previousFont);
    DeleteObject(titleFont);
    DeleteObject(bodyFont);

    BitBlt(target, 0, 0, width, height, memory, 0, 0, SRCCOPY);

    SelectObject(memory, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

LRESULT CALLBACK DiagnosticsWindow::WindowProcStatic(HWND hwnd, UINT message,
                                                     WPARAM wParam, LPARAM lParam) {
    DiagnosticsWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<DiagnosticsWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<DiagnosticsWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->WindowProc(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT DiagnosticsWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        Paint(dc, client);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_DPICHANGED:
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_CLOSE:
        // Hidden, not destroyed: the tray entry toggles it again cheaply.
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_NCDESTROY:
        if (hwnd_ == hwnd) {
            hwnd_ = nullptr;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace st
