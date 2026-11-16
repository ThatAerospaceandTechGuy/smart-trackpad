#include "App.h"

#include "Config.h"
#include "Install.h"
#include "Log.h"

#include <windows.h>

#include <memory>

namespace {

struct HandleCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
};

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Logging starts before installation so the update itself is recorded; the
    // real debug flag is applied once the configuration has been read.
    st::InitLogging(st::GetDataDirectory() / L"smarttrackpad.log", false);

    // Update the managed copy first and hand over to it, so the process that
    // keeps running is always the one the Startup shortcut points at.  Doing
    // this before creating the single-instance mutex keeps the handover child
    // from being rejected as a second instance.
    if (st::EnsureInstalled() && st::LaunchManagedCopy()) {
        st::ShutdownLogging();
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SmartTrackpad.Singleton");
    if (mutex == nullptr) {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0; // Another instance owns the trackpad.
    }

    std::unique_ptr<void, HandleCloser> mutexGuard(mutex);

    st::App app;
    if (!app.Initialize(instance)) {
        return 1;
    }
    return app.Run();
}
