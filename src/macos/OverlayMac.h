#pragma once

#include "OsdKind.h"

namespace st {

// The level overlay: a small floating HUD in the bottom centre of the screen,
// drawn with AppKit and faded out after two seconds.
//
// `Show` is safe to call from any thread; the work is marshalled to the main
// queue because AppKit is single-threaded.
class OverlayMac {
public:
    OverlayMac();
    ~OverlayMac();

    OverlayMac(const OverlayMac&) = delete;
    OverlayMac& operator=(const OverlayMac&) = delete;

    void SetShowPercentage(bool show);
    void Show(OsdKind kind, int percent, bool muted = false);

private:
    // Objective-C objects are stored as void* so this header stays usable from
    // plain C++ translation units.
    void* panel_ = nullptr;
    void* view_ = nullptr;
};

} // namespace st
