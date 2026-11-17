#pragma once

#include <filesystem>

namespace st {

// Path of the managed per-user copy (%LOCALAPPDATA%\SmartTrackpad\...).
[[nodiscard]] std::filesystem::path ManagedExecutablePath();

// Ensures a managed per-user copy of the executable exists and that the
// per-user Startup shortcut matches `startWithWindows`.  All failures are
// logged; they do not prevent the current process from running.
//
// Returns true when the managed copy was just refreshed and thus differs from
// the running executable; the caller should then hand over to it with
// LaunchManagedCopy() instead of continuing from the build output.
bool EnsureInstalled(bool startWithWindows);

// Starts the managed copy (the one EnsureInstalled() maintains) and returns
// true when the process was created. Used to hand over after an update.
bool LaunchManagedCopy();

// Startup shortcut state and management.
[[nodiscard]] bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enabled);

// Removes the Startup shortcut and the managed executable.  Returns true when
// the running process is the managed copy and therefore has to exit for the
// removal to complete (the file is then deleted when it is no longer in use).
bool RemoveManagedInstall();

} // namespace st
