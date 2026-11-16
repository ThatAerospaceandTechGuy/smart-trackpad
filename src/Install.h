#pragma once

namespace st {

// Ensures a managed per-user copy of the executable exists and that the
// per-user Startup shortcut points at it. All failures are logged; they do not
// prevent the current process from running.
void EnsureInstalled();

} // namespace st
