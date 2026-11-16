#include "Osd.h"

#include "Log.h"

#include <windows.h>
#include <objidl.h>
#include <dwmapi.h>
#include <gdiplus.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace st {
namespace {

constexpr wchar_t kOsdClassName[] = L"SmartTrackpadOsdWindow";

// Geometry measured from the Windows 11 hardware indicator at 125% DPI.
// All values are device-independent pixels (96 DPI).
constexpr int kCardWidth = 192;
constexpr int kCardHeight = 48;
constexpr int kCornerRadius = 8;
constexpr int kTrackLeft = 42;
constexpr int kTrackWidth = 110;
constexpr int kTrackHeight = 4;
constexpr int kIconLeft = 11;
constexpr int kIconBox = 18;
constexpr float kIconFontSize = 14.25f;
constexpr int kTextRightPadding = 9;
constexpr int kTextFontSize = 15;
constexpr int kBottomMargin = 13;

constexpr UINT_PTR kFadeTimer = 1;
constexpr UINT_PTR kHideTimer = 2;

// Posted by controller workers with the confirmed level; see Osd::Post.
constexpr UINT kApplyMessage = WM_APP + 40;
constexpr UINT kFadeIntervalMs = 16;
constexpr int kHoldMs = 2000;
constexpr int kFadeInMs = 120;
constexpr int kFadeOutMs = 160;

constexpr BYTE kDarkCard = 44;
constexpr BYTE kDarkTrack = 159;
constexpr BYTE kLightCard = 243;
constexpr BYTE kLightTrack = 154;

struct AccentPalette {
    bool valid = false;
    BYTE colors[8][3]{};
};

[[nodiscard]] bool IsFontAvailable(const wchar_t* name) {
    Gdiplus::FontFamily family(name);
    return family.GetLastStatus() == Gdiplus::Ok && family.IsAvailable();
}

[[nodiscard]] const wchar_t* FirstAvailableFont(
    std::initializer_list<const wchar_t*> names) {
    for (const wchar_t* name : names) {
        if (IsFontAvailable(name)) {
            return name;
        }
    }
    return L"Segoe UI";
}

[[nodiscard]] bool SystemUsesLightTheme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value,
                     &size) == ERROR_SUCCESS) {
        return value != 0;
    }
    return false;
}

[[nodiscard]] AccentPalette ReadAccentPalette() {
    AccentPalette palette;
    BYTE bytes[32]{};
    DWORD size = sizeof(bytes);

    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, bytes,
                     &size) != ERROR_SUCCESS ||
        size < 32) {
        return palette;
    }

    // The palette is stored as eight RGBX colors. Index 1 is AccentLight2,
    // which is the exact color Windows uses for the indicator's filled track.
    for (int index = 0; index < 8; ++index) {
        palette.colors[index][0] = bytes[index * 4 + 0];
        palette.colors[index][1] = bytes[index * 4 + 1];
        palette.colors[index][2] = bytes[index * 4 + 2];
    }
    palette.valid = true;
    return palette;
}

[[nodiscard]] Gdiplus::Color AccentColor(bool lightTheme) {
    const AccentPalette palette = ReadAccentPalette();
    if (palette.valid) {
        // Use Accent (index 3) on a light surface, AccentLight2 (index 1) on
        // the dark Mica-like surface.
        const int index = lightTheme ? 3 : 1;
        return Gdiplus::Color(255, palette.colors[index][0],
                              palette.colors[index][1],
                              palette.colors[index][2]);
    }

    COLORREF color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        BYTE red = static_cast<BYTE>((color >> 16) & 0xFF);
        BYTE green = static_cast<BYTE>((color >> 8) & 0xFF);
        BYTE blue = static_cast<BYTE>(color & 0xFF);
        if (!lightTheme) {
            // Approximate the lighter accent Windows uses on dark surfaces.
            red = static_cast<BYTE>((red + 255 * 3) / 4);
            green = static_cast<BYTE>((green + 255 * 3) / 4);
            blue = static_cast<BYTE>((blue + 255 * 3) / 4);
        }
        return Gdiplus::Color(255, red, green, blue);
    }

    return lightTheme ? Gdiplus::Color(255, 194, 28, 20)
                      : Gdiplus::Color(255, 243, 128, 100);
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect,
                    float radius) {
    const float diameter = std::min(radius * 2.0f,
                                    std::min(rect.Width, rect.Height));
    if (diameter <= 0.0f) {
        path.AddRectangle(rect);
        path.CloseFigure();
        return;
    }

    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter,
                270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter,
                rect.Y + rect.Height - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter,
                90.0f, 90.0f);
    path.CloseFigure();
}

} // namespace

class Osd::Impl {
public:
    Impl();
    ~Impl();

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void Show(OsdKind kind, int percent, bool muted = false);
    void Post(OsdKind kind, int percent, bool muted);

private:
    static LRESULT CALLBACK WindowProcStatic(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateOsdWindow();
    void Position();
    bool EnsureSurface();
    void ReleaseSurface();
    void Render();
    void Present();
    void StartFade(double target, int durationMs);
    void OnFadeTimer();
    void OnHideTimer();
    void HideNow();

    [[nodiscard]] int Scale(int dip) const;
    [[nodiscard]] float ScaleF(float dip) const;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    // Read by controller workers through Post(); updated only by the UI thread.
    std::atomic<HWND> postTarget_{nullptr};
    ULONG_PTR gdiplusToken_ = 0;
    bool gdiplusStarted_ = false;
    bool classRegistered_ = false;

    int dpi_ = 96;
    int width_ = 0;
    int height_ = 0;
    int left_ = 0;
    int top_ = 0;

    HBITMAP dib_ = nullptr;
    HDC memoryDc_ = nullptr;
    void* bits_ = nullptr;
    bool surfaceReady_ = false;

    OsdKind kind_ = OsdKind::Volume;
    int percent_ = 0;
    bool muted_ = false;
    bool visible_ = false;
    double opacity_ = 0.0;
    double fadeFrom_ = 0.0;
    double fadeTo_ = 0.0;
    ULONGLONG fadeStarted_ = 0;
    int fadeDurationMs_ = 0;
};

Osd::Impl::Impl() : instance_(GetModuleHandleW(nullptr)) {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr) !=
        Gdiplus::Ok) {
        LogMessage(LogLevel::Warning,
                   L"OSD unavailable: GDI+ could not be initialized.");
        return;
    }
    gdiplusStarted_ = true;
    CreateOsdWindow();
    Position();
}

Osd::Impl::~Impl() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    ReleaseSurface();
    if (classRegistered_ && instance_ != nullptr) {
        UnregisterClassW(kOsdClassName, instance_);
        classRegistered_ = false;
    }
    if (gdiplusStarted_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusStarted_ = false;
    }
}

bool Osd::Impl::CreateOsdWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &Impl::WindowProcStatic;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kOsdClassName;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (RegisterClassExW(&windowClass) != 0) {
        classRegistered_ = true;
    } else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        LogFormat(LogLevel::Warning,
                  L"OSD unavailable: RegisterClassEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW |
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kOsdClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance_,
        this);
    if (hwnd_ == nullptr) {
        LogFormat(LogLevel::Warning,
                  L"OSD unavailable: CreateWindowEx failed (error %lu).",
                  GetLastError());
        return false;
    }

    ShowWindow(hwnd_, SW_HIDE);
    return true;
}

void Osd::Impl::Show(OsdKind kind, int percent, bool muted) {
    if (hwnd_ == nullptr) {
        return;
    }

    percent = std::clamp(percent, 0, 100);
    const bool changed = kind_ != kind || percent_ != percent || muted_ != muted;
    kind_ = kind;
    percent_ = percent;
    muted_ = muted;

    Position();
    if (!EnsureSurface()) {
        return;
    }

    if (changed || !visible_) {
        Render();
    }

    if (!visible_) {
        opacity_ = 0.0;
        visible_ = true;
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        StartFade(1.0, kFadeInMs);
    } else if (opacity_ < 1.0 && fadeTo_ <= 0.0) {
        StartFade(1.0, kFadeInMs);
    }

    KillTimer(hwnd_, kHideTimer);
    SetTimer(hwnd_, kHideTimer, static_cast<UINT>(kHoldMs), nullptr);
    Present();
}

void Osd::Impl::Post(OsdKind kind, int percent, bool muted) {
    const HWND target = postTarget_.load(std::memory_order_relaxed);
    if (target == nullptr) {
        return;
    }

    // Runs on a controller worker thread: never touch window state here, just
    // hand the confirmed level to the UI thread.
    const WPARAM kindParam = kind == OsdKind::Volume ? 0u : 1u;
    const LPARAM valueParam =
        static_cast<LPARAM>(std::clamp(percent, 0, 100)) | (muted ? 0x10000 : 0);
    PostMessageW(target, kApplyMessage, kindParam, valueParam);
}

void Osd::Impl::Position() {
    if (hwnd_ == nullptr) {
        return;
    }

    const UINT dpi = GetDpiForWindow(hwnd_);
    if (dpi != 0) {
        dpi_ = static_cast<int>(dpi);
    }

    // Follow the monitor the user is working on rather than always using the
    // primary display; the touchpad is not bound to a particular screen.
    POINT cursor{};
    const HMONITOR monitor =
        GetCursorPos(&cursor)
            ? MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        return;
    }

    const int newWidth = Scale(kCardWidth);
    const int newHeight = Scale(kCardHeight);
    if (newWidth != width_ || newHeight != height_) {
        ReleaseSurface();
    }
    width_ = newWidth;
    height_ = newHeight;
    left_ = monitorInfo.rcMonitor.left +
            (monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left -
             width_) /
                2;
    top_ = monitorInfo.rcWork.bottom - Scale(kBottomMargin) - height_;
    if (top_ < monitorInfo.rcWork.top) {
        top_ = monitorInfo.rcWork.top;
    }

    SetWindowPos(hwnd_, HWND_TOPMOST, left_, top_, width_, height_,
                 SWP_NOACTIVATE);
}

bool Osd::Impl::EnsureSurface() {
    if (width_ <= 0 || height_ <= 0) {
        return false;
    }
    if (surfaceReady_ && dib_ != nullptr && memoryDc_ != nullptr) {
        return true;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width_;
    bitmapInfo.bmiHeader.biHeight = -height_;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    dib_ = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS, &bits_,
                            nullptr, 0);
    if (dib_ == nullptr || bits_ == nullptr) {
        ReleaseSurface();
        LogFormat(LogLevel::Warning,
                  L"OSD unavailable: CreateDIBSection failed (error %lu).",
                  GetLastError());
        return false;
    }

    memoryDc_ = CreateCompatibleDC(nullptr);
    if (memoryDc_ == nullptr) {
        ReleaseSurface();
        LogFormat(LogLevel::Warning,
                  L"OSD unavailable: CreateCompatibleDC failed (error %lu).",
                  GetLastError());
        return false;
    }

    SelectObject(memoryDc_, dib_);
    surfaceReady_ = true;
    return true;
}

void Osd::Impl::ReleaseSurface() {
    if (memoryDc_ != nullptr) {
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
    if (dib_ != nullptr) {
        DeleteObject(dib_);
        dib_ = nullptr;
    }
    bits_ = nullptr;
    surfaceReady_ = false;
}

void Osd::Impl::Render() {
    if (!EnsureSurface()) {
        return;
    }

    const bool light = SystemUsesLightTheme();
    const float cardWidth = static_cast<float>(width_);
    const float cardHeight = static_cast<float>(height_);
    const float radius = ScaleF(static_cast<float>(kCornerRadius));

    Gdiplus::Bitmap bitmap(width_, height_, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    Gdiplus::GraphicsPath cardPath;
    AddRoundedRect(cardPath, Gdiplus::RectF(0.0f, 0.0f, cardWidth, cardHeight),
                   radius);
    const BYTE cardValue = light ? kLightCard : kDarkCard;
    Gdiplus::SolidBrush cardBrush(
        Gdiplus::Color(255, cardValue, cardValue, cardValue));
    graphics.FillPath(&cardBrush, &cardPath);

    const int trackHeightPixels = Scale(kTrackHeight);
    const float trackHeight = static_cast<float>(trackHeightPixels);
    const float trackTop =
        static_cast<float>((height_ - trackHeightPixels) / 2);
    const float trackLeft = static_cast<float>(Scale(kTrackLeft));
    const float trackWidth = static_cast<float>(Scale(kTrackWidth));
    const float trackRadius = trackHeight * 0.5f;
    const BYTE trackValue = light ? kLightTrack : kDarkTrack;

    Gdiplus::GraphicsPath trackPath;
    AddRoundedRect(
        trackPath,
        Gdiplus::RectF(trackLeft, trackTop, trackWidth, trackHeight),
        trackRadius);
    Gdiplus::SolidBrush trackBrush(
        Gdiplus::Color(255, trackValue, trackValue, trackValue));
    graphics.FillPath(&trackBrush, &trackPath);

    const int fillPixels = static_cast<int>(std::floor(
        trackWidth * static_cast<float>(percent_) / 100.0f));
    if (fillPixels > 0) {
        graphics.SetClip(&trackPath, Gdiplus::CombineModeReplace);
        Gdiplus::SolidBrush fillBrush(AccentColor(light));
        graphics.FillRectangle(
            &fillBrush,
            Gdiplus::RectF(trackLeft, trackTop,
                           static_cast<float>(fillPixels), trackHeight));
        graphics.ResetClip();
    }

    const wchar_t* iconFamilyName = FirstAvailableFont(
        {L"Segoe Fluent Icons", L"Segoe MDL2 Assets", L"Segoe UI Symbol"});
    Gdiplus::FontFamily iconFamily(iconFamilyName);
    Gdiplus::Font iconFont(&iconFamily,
                           ScaleF(static_cast<float>(kIconFontSize)),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const wchar_t* glyph = L"\xE706";
    if (kind_ == OsdKind::Volume) {
        glyph = (muted_ || percent_ == 0) ? L"\xE74F" : L"\xE767";
    }

    Gdiplus::StringFormat iconFormat;
    iconFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
    iconFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    iconFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip |
                              Gdiplus::StringFormatFlagsNoWrap);
    const Gdiplus::Color foreground =
        light ? Gdiplus::Color(255, 26, 26, 26)
              : Gdiplus::Color(255, 255, 255, 255);
    Gdiplus::SolidBrush foregroundBrush(foreground);
    const float iconLeft = ScaleF(static_cast<float>(kIconLeft));
    const float iconWidth = ScaleF(static_cast<float>(kIconBox));
    const float iconTop = ScaleF(0.8f);
    graphics.DrawString(
        glyph, -1, &iconFont,
        Gdiplus::RectF(iconLeft, iconTop, iconWidth, cardHeight),
        &iconFormat, &foregroundBrush);

    const wchar_t* textFamilyName = FirstAvailableFont(
        {L"Segoe UI Variable Text", L"Segoe UI"});
    Gdiplus::FontFamily textFamily(textFamilyName);
    Gdiplus::Font textFont(&textFamily,
                           ScaleF(static_cast<float>(kTextFontSize)),
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    const std::wstring value = std::to_wstring(percent_);

    Gdiplus::StringFormat textFormat;
    textFormat.SetAlignment(Gdiplus::StringAlignmentFar);
    textFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    textFormat.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip |
                              Gdiplus::StringFormatFlagsNoWrap);
    const float textLeft = trackLeft + trackWidth + ScaleF(2.0f);
    const float textRight =
        cardWidth - ScaleF(static_cast<float>(kTextRightPadding));
    const float textTop = ScaleF(0.8f);
    graphics.DrawString(
        value.c_str(), -1, &textFont,
        Gdiplus::RectF(textLeft, textTop,
                       std::max(0.0f, textRight - textLeft), cardHeight),
        &textFormat, &foregroundBrush);

    Gdiplus::BitmapData data{};
    const Gdiplus::Rect lockRect(0, 0, width_, height_);
    if (bitmap.LockBits(&lockRect, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return;
    }

    auto* destination = static_cast<uint32_t*>(bits_);
    const auto* source = static_cast<const BYTE*>(data.Scan0);
    const int sourceStride = data.Stride;
    for (int y = 0; y < height_; ++y) {
        const BYTE* row =
            source + static_cast<std::ptrdiff_t>(y) * sourceStride;
        for (int x = 0; x < width_; ++x) {
            const BYTE blue = row[x * 4 + 0];
            const BYTE green = row[x * 4 + 1];
            const BYTE red = row[x * 4 + 2];
            const BYTE alpha = row[x * 4 + 3];
            const uint32_t premultipliedBlue =
                static_cast<uint32_t>(blue) * alpha / 255u;
            const uint32_t premultipliedGreen =
                static_cast<uint32_t>(green) * alpha / 255u;
            const uint32_t premultipliedRed =
                static_cast<uint32_t>(red) * alpha / 255u;
            destination[y * width_ + x] =
                (static_cast<uint32_t>(alpha) << 24) |
                (premultipliedRed << 16) | (premultipliedGreen << 8) |
                premultipliedBlue;
        }
    }
    bitmap.UnlockBits(&data);
}

void Osd::Impl::Present() {
    if (hwnd_ == nullptr || memoryDc_ == nullptr) {
        return;
    }

    POINT destination{left_, top_};
    SIZE size{width_, height_};
    POINT source{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha =
        static_cast<BYTE>(std::lround(std::clamp(opacity_, 0.0, 1.0) * 255.0));
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd_, nullptr, &destination, &size, memoryDc_,
                        &source, 0, &blend, ULW_ALPHA);
}

void Osd::Impl::StartFade(double target, int durationMs) {
    fadeFrom_ = opacity_;
    fadeTo_ = std::clamp(target, 0.0, 1.0);
    fadeStarted_ = GetTickCount64();
    fadeDurationMs_ = std::max(1, durationMs);
    SetTimer(hwnd_, kFadeTimer, kFadeIntervalMs, nullptr);
}

void Osd::Impl::OnFadeTimer() {
    const ULONGLONG now = GetTickCount64();
    const double elapsed = static_cast<double>(now - fadeStarted_) /
                           static_cast<double>(fadeDurationMs_);
    const double progress = std::clamp(elapsed, 0.0, 1.0);
    opacity_ = fadeFrom_ + (fadeTo_ - fadeFrom_) * progress;

    if (progress >= 1.0) {
        opacity_ = fadeTo_;
        KillTimer(hwnd_, kFadeTimer);
        if (opacity_ <= 0.001) {
            HideNow();
            return;
        }
    }
    Present();
}

void Osd::Impl::OnHideTimer() {
    KillTimer(hwnd_, kHideTimer);
    StartFade(0.0, kFadeOutMs);
}

void Osd::Impl::HideNow() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
    visible_ = false;
    opacity_ = 0.0;
    KillTimer(hwnd_, kFadeTimer);
    KillTimer(hwnd_, kHideTimer);
}

int Osd::Impl::Scale(int dip) const {
    return std::max(1, MulDiv(dip, dpi_, 96));
}

float Osd::Impl::ScaleF(float dip) const {
    return dip * static_cast<float>(dpi_) / 96.0f;
}

LRESULT CALLBACK Osd::Impl::WindowProcStatic(HWND hwnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    Impl* self = nullptr;

    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Impl*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            self->postTarget_.store(hwnd, std::memory_order_relaxed);
        }
    } else {
        self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self != nullptr) {
        return self->WindowProc(hwnd, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Osd::Impl::WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                              LPARAM lParam) {
    (void)lParam;
    switch (message) {
    case WM_TIMER:
        if (wParam == kFadeTimer) {
            OnFadeTimer();
        } else if (wParam == kHideTimer) {
            OnHideTimer();
        }
        return 0;

    case kApplyMessage:
        // Confirmed level from a controller worker: 0 = volume, 1 = brightness
        // in the low bit of wParam; the value and mute flag travel in lParam.
        Show(wParam == 0 ? OsdKind::Volume : OsdKind::Brightness,
             static_cast<int>(LOWORD(lParam)), (lParam & 0x10000) != 0);
        return 0;

    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd, &paint);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        if (visible_) {
            Position();
            Render();
            Present();
        }
        return 0;

    case WM_NCDESTROY:
        if (hwnd_ == hwnd) {
            hwnd_ = nullptr;
            postTarget_.store(nullptr, std::memory_order_relaxed);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;

    default:
        break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

Osd::Osd() : impl_(std::make_unique<Impl>()) {}

Osd::~Osd() = default;

void Osd::Show(OsdKind kind, int percent, bool muted) {
    if (impl_) {
        impl_->Show(kind, percent, muted);
    }
}

void Osd::Post(OsdKind kind, int percent, bool muted) {
    if (impl_) {
        impl_->Post(kind, percent, muted);
    }
}

} // namespace st
