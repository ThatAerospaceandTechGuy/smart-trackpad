#pragma once

namespace st {

// Ensures a managed per-user copy of the executable exists and that the
// per-user Startup shortcut points at it. All failures are logged; they do not
// prevent the current process from running.
//
// Returns true when the managed copy was just refreshed and thus differs from
// the running executable; the caller should then hand over to it with
// LaunchManagedCopy() instead of continuing from the build output.
bool EnsureInstalled();

// Starts the managed copy (the one EnsureInstalled() maintains) and returns
// true when the process was created. Used to hand over after an update.
bool LaunchManagedCopy();

} // namespace st
