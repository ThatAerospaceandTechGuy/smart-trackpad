#include "OsdLinux.h"

#include "Log.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#if defined(SMARTTRACKPAD_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace st {

struct X11Overlay::Impl {
#if defined(SMARTTRACKPAD_HAVE_X11)
    Display* display = nullptr;
    int screen = 0;
    Window window = 0;
    GC gc = nullptr;
    XFontStruct* font = nullptr;
#endif
};

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 64;
constexpr int kMarginFromBottom = 96;
constexpr int kVisibleMs = 2000;

// Windows 11 indicator colours: dark card, light bar, white label.
constexpr unsigned long kCardColor = 0x1F1F1F;
constexpr unsigned long kBarTrackColor = 0x5A5A5A;
constexpr unsigned long kBarFillColor = 0xD9D9D9;
constexpr unsigned long kMutedFillColor = 0x8C8C8C;
constexpr unsigned long kTextColor = 0xFFFFFF;

#if defined(SMARTTRACKPAD_HAVE_X11)
unsigned long AllocateColour(Display* display, int screen, unsigned long rgb) {
    XColor colour{};
    colour.red = static_cast<unsigned short>(((rgb >> 16) & 0xFF) * 257);
    colour.green = static_cast<unsigned short>(((rgb >> 8) & 0xFF) * 257);
    colour.blue = static_cast<unsigned short>((rgb & 0xFF) * 257);
    colour.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(display, DefaultColormap(display, screen), &colour)) {
        return BlackPixel(display, screen);
    }
    return colour.pixel;
}
#endif

} // namespace

X11Overlay::X11Overlay() : impl_(std::make_unique<Impl>()) {
    if (pipe2(wakePipe_, O_NONBLOCK | O_CLOEXEC) != 0) {
        wakePipe_[0] = -1;
        wakePipe_[1] = -1;
        return;
    }

#if defined(SMARTTRACKPAD_HAVE_X11)
    XInitThreads();
    impl_->display = XOpenDisplay(nullptr);
    if (impl_->display == nullptr) {
        LogMessage(LogLevel::Info,
                   L"Overlay: no X11 display (Wayland-only session?); levels are "
                   L"applied without an overlay.");
        return;
    }

    impl_->screen = DefaultScreen(impl_->display);

    XSetWindowAttributes attributes{};
    attributes.override_redirect = True;
    attributes.background_pixel =
        AllocateColour(impl_->display, impl_->screen, kCardColor);

    impl_->window = XCreateWindow(
        impl_->display, RootWindow(impl_->display, impl_->screen), 0, 0, kWidth,
        kHeight, 0, CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWBackPixel, &attributes);

    XClassHint hint{};
    hint.res_name = const_cast<char*>("smarttrackpad");
    hint.res_class = const_cast<char*>("SmartTrackpad");
    XSetClassHint(impl_->display, impl_->window, &hint);

    impl_->gc = XCreateGC(impl_->display, impl_->window, 0, nullptr);
    impl_->font = XLoadQueryFont(impl_->display, "fixed");
    if (impl_->font != nullptr) {
        XSetFont(impl_->display, impl_->gc, impl_->font->fid);
    }

    available_ = true;
    thread_ = std::jthread([this](std::stop_token token) { Run(token); });
#else
    LogMessage(LogLevel::Info,
               L"Overlay: built without X11 support; levels are applied without an "
               L"overlay.");
#endif
}

X11Overlay::~X11Overlay() {
    if (thread_.joinable()) {
        thread_.request_stop();
        Wake();
        thread_.join();
    }

#if defined(SMARTTRACKPAD_HAVE_X11)
    if (impl_ != nullptr && impl_->display != nullptr) {
        if (impl_->font != nullptr) {
            XFreeFont(impl_->display, impl_->font);
        }
        if (impl_->gc != nullptr) {
            XFreeGC(impl_->display, impl_->gc);
        }
        XDestroyWindow(impl_->display, impl_->window);
        XCloseDisplay(impl_->display);
    }
#endif

    for (int& end : wakePipe_) {
        if (end >= 0) {
            close(end);
            end = -1;
        }
    }
}

void X11Overlay::Wake() {
    if (wakePipe_[1] >= 0) {
        const char byte = 1;
        (void)!write(wakePipe_[1], &byte, 1);
    }
}

void X11Overlay::SetShowPercentage(bool show) {
    std::lock_guard lock(mutex_);
    showPercentage_ = show;
}

void X11Overlay::Show(OsdKind kind, int percent, bool muted) {
    if (!available_) {
        return;
    }

    {
        std::lock_guard lock(mutex_);
        pending_ = Request{kind, std::clamp(percent, 0, 100), muted};
        hasPending_ = true;
    }
    Wake();
}

void X11Overlay::Run(std::stop_token token) {
#if defined(SMARTTRACKPAD_HAVE_X11)
    Display* display = impl_->display;
    const int screen = impl_->screen;

    const unsigned long card = AllocateColour(display, screen, kCardColor);
    const unsigned long track = AllocateColour(display, screen, kBarTrackColor);
    const unsigned long fill = AllocateColour(display, screen, kBarFillColor);
    const unsigned long mutedFill = AllocateColour(display, screen, kMutedFillColor);
    const unsigned long text = AllocateColour(display, screen, kTextColor);

    auto visibleUntil = std::chrono::steady_clock::now();
    bool visible = false;

    while (!token.stop_requested()) {
        pollfd descriptor{};
        descriptor.fd = wakePipe_[0];
        descriptor.events = POLLIN;
        (void)poll(&descriptor, 1, 100);

        if ((descriptor.revents & POLLIN) != 0) {
            char buffer[64];
            while (read(wakePipe_[0], buffer, sizeof(buffer)) > 0) {
            }
        }

        Request request{};
        bool draw = false;
        bool showLabel = true;
        {
            std::lock_guard lock(mutex_);
            if (hasPending_) {
                request = pending_;
                hasPending_ = false;
                draw = true;
            }
            showLabel = showPercentage_;
        }

        if (draw) {
            const int x = (DisplayWidth(display, screen) - kWidth) / 2;
            const int y = DisplayHeight(display, screen) - kHeight - kMarginFromBottom;
            XMoveResizeWindow(display, impl_->window, x, y, kWidth, kHeight);

            XSetForeground(display, impl_->gc, card);
            XFillRectangle(display, impl_->window, impl_->gc, 0, 0, kWidth, kHeight);

            const int barLeft = 16;
            const int barWidth = kWidth - 32;
            const int barTop = kHeight - 22;

            XSetForeground(display, impl_->gc, track);
            XFillRectangle(display, impl_->window, impl_->gc, barLeft, barTop, barWidth,
                           5);

            XSetForeground(display, impl_->gc, request.muted ? mutedFill : fill);
            XFillRectangle(display, impl_->window, impl_->gc, barLeft, barTop,
                           std::max(1, barWidth * request.percent / 100), 5);

            if (impl_->font != nullptr && showLabel) {
                char label[16];
                std::snprintf(label, sizeof(label), "%d%%", request.percent);
                XSetForeground(display, impl_->gc, text);
                XDrawString(display, impl_->window, impl_->gc, barLeft, 32, label,
                            static_cast<int>(std::strlen(label)));
            }

            XMapRaised(display, impl_->window);
            XFlush(display);
            visible = true;
            visibleUntil = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(kVisibleMs);
        }

        if (visible && std::chrono::steady_clock::now() >= visibleUntil) {
            XUnmapWindow(display, impl_->window);
            XFlush(display);
            visible = false;
        }
    }
#else
    (void)token;
#endif
}

} // namespace st
