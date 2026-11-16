#include "Install.h"

#include "ComPtr.h"
#include "Config.h"
#include "Log.h"

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

std::filesystem::path ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;) {
        const DWORD length = GetModuleFileNameW(
            nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    return std::filesystem::path(buffer);
}

bool SamePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
    if (_wcsicmp(left.lexically_normal().c_str(),
                 right.lexically_normal().c_str()) == 0) {
        return true;
    }

    std::error_code error;
    if (std::filesystem::exists(left, error)) {
        error.clear();
        if (std::filesystem::exists(right, error)) {
            error.clear();
            return std::filesystem::equivalent(left, right, error) && !error;
        }
    }
    return false;
}

bool SourceIsNewer(const std::filesystem::path& source,
                   const std::filesystem::path& target) {
    std::error_code sourceError;
    std::error_code targetError;
    const auto sourceTime =
        std::filesystem::last_write_time(source, sourceError);
    const auto targetTime =
        std::filesystem::last_write_time(target, targetError);
    return !sourceError && !targetError && sourceTime > targetTime;
}

bool CreateStartupShortcut(const std::filesystem::path& target) {
    PWSTR startupFolder = nullptr;
    const HRESULT folderResult = SHGetKnownFolderPath(
        FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &startupFolder);
    if (FAILED(folderResult) || startupFolder == nullptr) {
        LogFormat(LogLevel::Warning,
                  L"Install: could not locate the Startup folder (0x%08lX).",
                  static_cast<unsigned long>(folderResult));
        if (startupFolder != nullptr) {
            CoTaskMemFree(startupFolder);
        }
        return false;
    }

    const std::filesystem::path shortcutPath =
        std::filesystem::path(startupFolder) / kStartupShortcutName;
    CoTaskMemFree(startupFolder);

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
    return true;
}

} // namespace

void EnsureInstalled() {
    const std::filesystem::path source = ExecutablePath();
    if (source.empty()) {
        LogMessage(LogLevel::Warning,
                   L"Install: could not determine the executable path.");
        return;
    }

    const std::filesystem::path target =
        GetDataDirectory() / kManagedExecutableName;

    if (SamePath(source, target)) {
        LogFormat(LogLevel::Info,
                  L"Install: running the managed copy at \"%ls\".",
                  target.c_str());
        CreateStartupShortcut(target);
        return;
    }

    std::error_code error;
    const bool targetExists = std::filesystem::exists(target, error);
    const bool shouldCopy = !targetExists || SourceIsNewer(source, target);

    if (shouldCopy) {
        error.clear();
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) {
            LogFormat(LogLevel::Warning,
                      L"Install: could not create \"%ls\" for the managed copy.",
                      target.parent_path().c_str());
            return;
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
        } else if (!MoveFileExW(temporary.c_str(), target.c_str(),
                                MOVEFILE_REPLACE_EXISTING |
                                    MOVEFILE_WRITE_THROUGH)) {
            LogFormat(LogLevel::Warning,
                      L"Install: could not activate the managed copy at \"%ls\" "
                      L"(error %lu).",
                      target.c_str(), GetLastError());
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
        } else {
            LogFormat(LogLevel::Info,
                      L"Install: copied \"%ls\" to the managed location \"%ls\".",
                      source.c_str(), target.c_str());
        }
    } else {
        LogFormat(LogLevel::Info,
                  L"Install: keeping the existing managed copy at \"%ls\".",
                  target.c_str());
    }

    error.clear();
    if (!std::filesystem::exists(target, error)) {
        LogMessage(LogLevel::Warning,
                   L"Install: no managed executable is available; the startup "
                   L"shortcut was not changed.");
        return;
    }

    CreateStartupShortcut(target);
}

} // namespace st
