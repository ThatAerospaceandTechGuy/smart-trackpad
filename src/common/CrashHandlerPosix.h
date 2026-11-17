#pragma once

namespace st {

// Installs fatal-signal handlers that write a backtrace to the log (and to
// stderr) before the process dies.  Best effort: a signal raised on an
// alternate stack is reported but the stack trace may be incomplete.
void InstallCrashHandler();

} // namespace st
