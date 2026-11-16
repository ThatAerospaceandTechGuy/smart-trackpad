#include "App.h"

#include "Config.h"
#include "CrashHandler.h"
#include "Install.h"
#include "Log.h"

#include <windows.h>
#include <shellapi.h>

#include <cwchar>
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

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // Switches: --settings, --zone-test, --uninstall.  The first two also work
    // while the app is already running: the request is forwarded to that
    // instance.
    st::StartupAction startupAction = st::StartupAction::None;
    bool uninstall = false;
    if (commandLine != nullptr) {
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(commandLine, &argumentCount);
        if (arguments != nullptr) {
            // wWinMain receives the arguments only, so index 0 is the first
            // switch rather than the program name.
            for (int index = 0; index < argumentCount; ++index) {
                if (_wcsicmp(arguments[index], L"--settings") == 0) {
                    startupAction = st::StartupAction::Settings;
                } else if (_wcsicmp(arguments[index], L"--zone-test") == 0) {
                    startupAction = st::StartupAction::ZoneTest;
                } else if (_wcsicmp(arguments[index], L"--uninstall") == 0) {
                    uninstall = true;
                }
            }
            LocalFree(arguments);
        }
    }

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Logging starts before installation so the update itself is recorded; the
    // real debug flag is applied once the configuration has been read.
    st::InitLogging(st::GetDataDirectory() / L"smarttrackpad.log", false);
    st::InstallCrashHandler();
    st::LogFormat(st::LogLevel::Info, L"Smart Trackpad %ls starting.",
                  SMARTTRACKPAD_VERSION);

    if (uninstall) {
        // Ask the running instance to stop, then remove the managed copy and the
        // Startup shortcut.  Removing a file that is still executing renames it
        // and schedules the delete for the next sign-in.
        const HWND running = FindWindowW(st::kControlWindowClassName, nullptr);
        const UINT command = RegisterWindowMessageW(st::kCommandMessageName);
        const bool askedToExit =
            running != nullptr && command != 0 &&
            PostMessageW(running, command, st::kCommandExit, 0) != FALSE;
        st::LogFormat(st::LogLevel::Info,
                      L"Uninstall: running instance 0x%llX, exit request %ls.",
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(running)),
                      askedToExit ? L"sent" : L"not needed");
        if (askedToExit) {
            Sleep(700);
        }

        const bool rebootPending = st::RemoveManagedInstall();
        st::LogMessage(st::LogLevel::Info,
                       rebootPending
                           ? L"Uninstall: the managed copy is in use and will be "
                             L"deleted at the next sign-in."
                           : L"Uninstall: the managed copy and the Startup "
                             L"shortcut were removed.");
        st::ShutdownLogging();
        return 0;
    }

    // The startup preference lives in the configuration, so read it before the
    // installation step that applies it.  App::Initialize() reads it again for
    // the gesture settings.
    const st::AppConfig startupConfig = st::LoadConfig(st::GetConfigPath());

    // Update the managed copy first and hand over to it, so the process that
    // keeps running is always the one the Startup shortcut points at.  Doing
    // this before creating the single-instance mutex keeps the handover child
    // from being rejected as a second instance.
    if (st::EnsureInstalled(startupConfig.startWithWindows) && st::LaunchManagedCopy()) {
        st::ShutdownLogging();
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SmartTrackpad.Singleton");
    if (mutex == nullptr) {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);

        // Forward the requested command to the instance that owns the trackpad.
        if (startupAction != st::StartupAction::None) {
            const HWND running =
                FindWindowW(st::kControlWindowClassName, nullptr);
            const UINT command = RegisterWindowMessageW(st::kCommandMessageName);
            st::LogFormat(st::LogLevel::Info,
                          L"Command: forwarding to existing instance (window "
                          L"0x%llX, message 0x%04X).",
                          static_cast<unsigned long long>(
                              reinterpret_cast<uintptr_t>(running)),
                          command);
            if (running != nullptr && command != 0) {
                PostMessageW(running, command,
                             startupAction == st::StartupAction::Settings
                                 ? st::kCommandOpenSettings
                                 : st::kCommandOpenZoneTest,
                             0);
            }
        }
        return 0; // Another instance owns the trackpad.
    }

    std::unique_ptr<void, HandleCloser> mutexGuard(mutex);

    st::App app;
    app.SetStartupAction(startupAction);
    if (!app.Initialize(instance)) {
        return 1;
    }
    return app.Run();
}
