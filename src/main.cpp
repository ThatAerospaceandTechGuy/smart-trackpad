#include "App.h"

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
