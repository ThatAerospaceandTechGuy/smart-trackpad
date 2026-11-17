#pragma once

namespace st {

// Which symbol the level overlay shows.  Shared by the platforms that draw an
// overlay (Windows GDI+, macOS AppKit, Linux X11).
enum class OsdKind {
    Volume,
    Brightness,
};

} // namespace st
