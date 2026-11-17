#include "CrashHandlerPosix.h"

#include "Log.h"
#include "Text.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__has_include)
#if __has_include(<execinfo.h>)
#define SMARTTRACKPAD_HAS_BACKTRACE 1
#include <execinfo.h>
#endif
#endif

namespace st {
namespace {

constexpr int kMaxFrames = 32;

const char* SignalName(int signalNumber) {
    switch (signalNumber) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGABRT:
        return "SIGABRT (abort)";
    case SIGBUS:
        return "SIGBUS (bad address)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    default:
        return "fatal signal";
    }
}

void Handler(int signalNumber) {
    // Nothing here allocates: the process is already in an unusable state.
    char message[160]{};
    std::snprintf(message, sizeof(message), "Fatal: %s (%d).",
                  SignalName(signalNumber), signalNumber);
    LogMessageBestEffort(LogLevel::Error, text::ToWide(message));
    std::fputs(message, stderr);
    std::fputc('\n', stderr);

#if defined(SMARTTRACKPAD_HAS_BACKTRACE)
    void* frames[kMaxFrames]{};
    const int count = backtrace(frames, kMaxFrames);
    backtrace_symbols_fd(frames, count, fileno(stderr));
#endif

    // Restore the default action and re-raise so the shell sees the real
    // termination status instead of a swallowed crash.
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

} // namespace

void InstallCrashHandler() {
    for (const int signalNumber :
         {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) {
        std::signal(signalNumber, &Handler);
    }
}

} // namespace st
