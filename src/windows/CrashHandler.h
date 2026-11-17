#pragma once

namespace st {

// Installs handlers for unhandled exceptions, std::terminate, pure virtual
// calls and invalid CRT parameters.  Each handler appends a line to the log and
// tries to write a minidump next to it, so a background process that dies
// leaves evidence instead of vanishing from the tray.
void InstallCrashHandler();

} // namespace st
