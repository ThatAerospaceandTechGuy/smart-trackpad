#pragma once

#include <memory>
#include <mutex>
#include <thread>

#include "OsdKind.h"

namespace st {

// The level overlay.
//
// Plain X11 (no GTK, Qt or cairo dependency): an override-redirect window with a
// card, a level bar and the percentage, hidden again after two seconds.  It owns
// its display connection and a thread, so drawing never blocks gesture handling.
//
// A Wayland-only session has no X11 display; there the overlay stays disabled
// and the log says so once.  A build without X11 headers gets a no-op overlay.
class X11Overlay {
public:
    X11Overlay();
    ~X11Overlay();

    X11Overlay(const X11Overlay&) = delete;
    X11Overlay& operator=(const X11Overlay&) = delete;

    // True when a display could be opened and the overlay can be drawn.
    [[nodiscard]] bool Available() const noexcept { return available_; }

    void SetShowPercentage(bool show);

    // Thread safe; shows the requested value immediately.
    void Show(OsdKind kind, int percent, bool muted = false);

private:
    struct Impl;

    struct Request {
        OsdKind kind = OsdKind::Volume;
        int percent = 0;
        bool muted = false;
    };

    void Run(std::stop_token token);
    void Wake();

    std::mutex mutex_;
    std::unique_ptr<Impl> impl_;
    Request pending_{};
    bool hasPending_ = false;
    bool available_ = false;
    bool showPercentage_ = true;
    int wakePipe_[2] = {-1, -1};
    std::jthread thread_;
};

} // namespace st
