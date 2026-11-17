#include "Install.h"

#include "ComPtr.h"
#include "Config.h"
#include "Log.h"
#include "PathCompare.h"
#include "Platform.h"

#include <windows.h>
#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>

namespace st {
namespace {

constexpr wchar_t kManagedExecutableName[] = L"SmartTrackpad.exe";
constexpr wchar_t kStartupShortcutName[] = L"SmartTrackpad.lnk";

std::filesystem::path StartupShortcutPath() {
    PWSTR startupFolder = nullptr;
    const HRESULT result =
        SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &startupFolder);
    if (FAILED(result) || startupFolder == nullptr) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not locate the Startup folder (0x%08lX).",
                  static_cast<unsigned long>(result));
        if (startupFolder != nullptr) {
            CoTaskMemFree(startupFolder);
        }
        return {};
    }

    const std::filesystem::path path =
        std::filesystem::path(startupFolder) / kStartupShortcutName;
    CoTaskMemFree(startupFolder);
    return path;
}

// Reads the target of an existing .lnk.  Used to spot Startup entries that
// launch a different (usually older) SmartTrackpad.exe, which would otherwise
// start a second, unrelated build at sign-in.
std::filesystem::path ReadShortcutTarget(const std::filesystem::path& shortcut) {
    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        return {};
    }

    std::filesystem::path target;
    {
        ComPtr<IShellLinkW> link;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_IShellLinkW,
                                       reinterpret_cast<void**>(link.Put())))) {
            ComPtr<IPersistFile> file;
            if (SUCCEEDED(link->QueryInterface(
                    IID_IPersistFile, reinterpret_cast<void**>(file.Put()))) &&
                SUCCEEDED(file->Load(shortcut.c_str(), STGM_READ))) {
                wchar_t buffer[MAX_PATH * 2]{};
                if (SUCCEEDED(link->GetPath(buffer, static_cast<int>(std::size(buffer)),
                                            nullptr, SLGP_RAWPATH))) {
                    target = buffer;
                }
            }
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }
    return target;
}

// Removes Startup entries that launch a SmartTrackpad.exe other than the managed
// copy.  Returns the number of entries removed.
int RemoveConflictingStartupShortcuts(const std::filesystem::path& keep) {
    const std::filesystem::path folder = StartupShortcutPath().parent_path();
    if (folder.empty()) {
        return 0;
    }

    int removed = 0;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end;
         it.increment(error)) {
        const std::filesystem::path candidate = it->path();
        if (candidate.extension() != L".lnk") {
            continue;
        }
        if (_wcsicmp(candidate.filename().c_str(), kStartupShortcutName) == 0) {
            continue;
        }

        const std::filesystem::path target = ReadShortcutTarget(candidate);
        if (target.empty() || _wcsicmp(target.filename().c_str(),
                                       L"SmartTrackpad.exe") != 0) {
            continue;
        }
        if (SamePath(target, keep)) {
            continue;
        }

        std::error_code removeError;
        if (std::filesystem::remove(candidate, removeError)) {
            ++removed;
            LogFormat(LogLevel::Warning,
                      L"Install: removed the stale Startup shortcut \"%ls\" "
                      L"(it launched \"%ls\").",
                      candidate.filename().c_str(), target.c_str());
        }
    }
    return removed;
}

bool CreateStartupShortcut(const std::filesystem::path& target) {
    const std::filesystem::path shortcutPath = StartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }

    const HRESULT comResult =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        LogFormat(LogLevel::Warning,
                  L"Install: COM initialization for the Startup shortcut failed "
                  L"(0x%08lX).",
                  static_cast<unsigned long>(comResult));
        return false;
    }

    HRESULT result = E_FAIL;
    {
        ComPtr<IShellLinkW> link;
        result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IShellLinkW,
                                  reinterpret_cast<void**>(link.Put()));
        if (SUCCEEDED(result)) {
            result = link->SetPath(target.c_str());
        }
        if (SUCCEEDED(result)) {
            result = link->SetWorkingDirectory(target.parent_path().c_str());
        }
        if (SUCCEEDED(result)) {
            result = link->SetDescription(L"Smart Trackpad");
        }
        if (SUCCEEDED(result)) {
            result = link->SetIconLocation(target.c_str(), 0);
        }

        ComPtr<IPersistFile> file;
        if (SUCCEEDED(result)) {
            result = link->QueryInterface(
                IID_IPersistFile, reinterpret_cast<void**>(file.Put()));
        }
        if (SUCCEEDED(result)) {
            result = file->Save(shortcutPath.c_str(), TRUE);
        }
    }

    if (shouldUninitialize) {
        CoUninitialize();
    }

    if (FAILED(result)) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not create the Startup shortcut (0x%08lX).",
                  static_cast<unsigned long>(result));
        return false;
    }

    LogFormat(LogLevel::Info, L"Install: Startup shortcut points to \"%ls\".",
              target.c_str());
    RemoveConflictingStartupShortcuts(target);
    return true;
}

bool RemoveStartupShortcut() {
    const std::filesystem::path shortcutPath = StartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }

    std::error_code error;
    if (!std::filesystem::exists(shortcutPath, error)) {
        return true;
    }
    if (!std::filesystem::remove(shortcutPath, error)) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not remove the Startup shortcut (error %d).",
                  error.value());
        return false;
    }

    LogMessage(LogLevel::Info, L"Install: Startup shortcut removed.");
    return true;
}

} // namespace

std::filesystem::path ManagedExecutablePath() {
    return GetDataDirectory() / kManagedExecutableName;
}

void CleanupStaleCopies() {
    const std::filesystem::path managed = ManagedExecutablePath();
    for (const wchar_t* suffix : {L".old", L".new"}) {
        std::filesystem::path stale = managed;
        stale += suffix;

        std::error_code error;
        if (!std::filesystem::exists(stale, error)) {
            continue;
        }

        error.clear();
        if (std::filesystem::remove(stale, error)) {
            LogFormat(LogLevel::Info, L"Install: removed the stale copy \"%ls\".",
                      stale.c_str());
        }
    }
}

bool EnsureInstalled(bool startWithWindows) {
    CleanupStaleCopies();

    const std::filesystem::path source = ExecutablePath();
    if (source.empty()) {
        LogMessage(LogLevel::Warning,
                   L"Install: could not determine the executable path.");
        return false;
    }

    const std::filesystem::path target = ManagedExecutablePath();

    auto applyStartupPreference = [startWithWindows](const std::filesystem::path& path) {
        if (startWithWindows) {
            CreateStartupShortcut(path);
        } else {
            RemoveStartupShortcut();
        }
    };

    if (SamePath(source, target)) {
        LogFormat(LogLevel::Info,
                  L"Install: running the managed copy at \"%ls\".",
                  target.c_str());
        applyStartupPreference(target);
        return false;
    }

    std::error_code error;
    const bool targetExists = std::filesystem::exists(target, error);
    const bool shouldCopy = !targetExists || SourceIsNewer(source, target);

    if (!shouldCopy) {
        LogFormat(LogLevel::Info,
                  L"Install: keeping the existing managed copy at \"%ls\".",
                  target.c_str());
        applyStartupPreference(target);
        return false;
    }

    error.clear();
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not create \"%ls\" for the managed copy.",
                  target.parent_path().c_str());
        return false;
    }

    std::filesystem::path temporary = target;
    temporary += L".new";

    error.clear();
    std::filesystem::copy_file(source, temporary,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not copy \"%ls\" to \"%ls\".",
                  source.c_str(), temporary.c_str());
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }

    if (!MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not activate the managed copy at \"%ls\" "
                  L"(error %lu). The current build keeps running.",
                  target.c_str(), GetLastError());
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return false;
    }

    LogFormat(LogLevel::Info,
              L"Install: copied \"%ls\" to the managed location \"%ls\".",
              source.c_str(), target.c_str());

    applyStartupPreference(target);

    // The managed copy now holds this build; hand over so the Startup shortcut
    // and the running process always execute the same file.
    return true;
}

bool LaunchManagedCopy() {
    const std::filesystem::path target = ManagedExecutablePath();

    std::error_code error;
    if (!std::filesystem::exists(target, error)) {
        LogFormat(LogLevel::Warning,
                  L"Install: \"%ls\" is missing; staying in the current "
                  L"process.",
                  target.c_str());
        return false;
    }

    std::wstring commandLine = L"\"" + target.wstring() + L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    const BOOL created = CreateProcessW(
        target.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
        target.parent_path().c_str(), &startup, &process);
    if (!created) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not start the managed copy at \"%ls\" "
                  L"(error %lu); staying in the current process.",
                  target.c_str(), GetLastError());
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    LogFormat(LogLevel::Info,
              L"Install: handing over to the managed copy at \"%ls\".",
              target.c_str());
    return true;
}

bool IsStartWithWindowsEnabled() {
    const std::filesystem::path shortcutPath = StartupShortcutPath();
    if (shortcutPath.empty()) {
        return false;
    }

    std::error_code error;
    return std::filesystem::exists(shortcutPath, error);
}

bool SetStartWithWindows(bool enabled) {
    if (enabled) {
        const std::filesystem::path target = ManagedExecutablePath();
        std::error_code error;
        if (!std::filesystem::exists(target, error)) {
            LogMessage(LogLevel::Warning,
                       L"Install: no managed copy exists yet; the Startup "
                       L"shortcut was not created.");
            return false;
        }
        return CreateStartupShortcut(target);
    }
    return RemoveStartupShortcut();
}

bool RemoveManagedInstall() {
    RemoveStartupShortcut();

    const std::filesystem::path managed = ManagedExecutablePath();
    const bool runningManagedCopy = SamePath(ExecutablePath(), managed);

    std::error_code error;
    if (!std::filesystem::exists(managed, error)) {
        return runningManagedCopy;
    }

    if (std::filesystem::remove(managed, error)) {
        LogFormat(LogLevel::Info, L"Install: removed \"%ls\".", managed.c_str());
        return false;
    }

    // The executable is running: rename it out of the way (allowed while in
    // use) and let the session delete the renamed file at the next boot.
    std::filesystem::path stale = managed;
    stale += L".old";

    std::error_code removeError;
    std::filesystem::remove(stale, removeError);

    if (!MoveFileExW(managed.c_str(), stale.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not rename \"%ls\" for removal (error %lu).",
                  managed.c_str(), GetLastError());
        return runningManagedCopy;
    }

    // MOVEFILE_DELAY_UNTIL_REBOOT needs administrator rights, so treat it as a
    // hint only: the leftover is deleted by CleanupStaleCopies() on the next
    // launch either way.
    if (MoveFileExW(stale.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
        LogFormat(LogLevel::Info,
                  L"Install: \"%ls\" was scheduled for deletion."
                  , stale.c_str());
    } else {
        LogFormat(LogLevel::Info,
                  L"Install: \"%ls\" is left in place and will be removed the "
                  L"next time Smart Trackpad starts.",
                  stale.c_str());
    }

    return true;
}

} // namespace st
