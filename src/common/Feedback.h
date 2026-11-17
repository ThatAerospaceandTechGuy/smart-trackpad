#pragma once

#include <functional>

namespace st {

// What a controller reports back after it actually changed hardware.  The
// gesture engine shows the requested value immediately; this feedback lets the
// overlay converge on the truth (a muted endpoint, a backend that refused the
// write, a device that clamped the value).
struct LevelFeedback {
    int percent = 0;
    bool muted = false;
};

// Invoked from the controller's worker thread.
using LevelFeedbackFn = std::function<void(const LevelFeedback&)>;

} // namespace st
