#pragma once

#include <memory>

namespace st {

enum class OsdKind {
    Volume,
    Brightness,
};

// A small, click-through, topmost overlay that mirrors the Windows 11
// hardware indicator. It is intentionally application-drawn: Windows exposes
// no supported API for showing the native brightness HUD.
class Osd {
public:
    Osd();
    ~Osd();

    Osd(const Osd&) = delete;
    Osd& operator=(const Osd&) = delete;

    // Values are absolute percentages in the same range as the edge controls.
    // `muted` only affects the speaker icon.  Must be called on the UI thread.
    void Show(OsdKind kind, int percent, bool muted = false);

    // Thread-safe variant for controller workers: posts the confirmed value to
    // the overlay window instead of touching it directly.
    void Post(OsdKind kind, int percent, bool muted = false);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace st
