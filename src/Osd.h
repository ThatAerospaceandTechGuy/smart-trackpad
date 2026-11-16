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
    void Show(OsdKind kind, int percent);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace st
