#include "Config.h"

#include "Log.h"

#include <windows.h>

#include <utility>
#include <vector>

namespace st {
namespace {

constexpr DWORD kDebounceMs = 300;

bool NotifyMatches(const std::vector<BYTE>& buffer, DWORD bytes,
                   const std::wstring& fileName) {
    DWORD offset = 0;
    for (;;) {
        if (offset + sizeof(FILE_NOTIFY_INFORMATION) > bytes) {
            return false;
        }

        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
            buffer.data() + offset);
        const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
        if (_wcsicmp(name.c_str(), fileName.c_str()) == 0) {
            return true;
        }

        if (info->NextEntryOffset == 0) {
            return false;
        }
        offset += info->NextEntryOffset;
    }
}

} // namespace

struct ConfigWatcher::Impl {
    void* directory = nullptr;
    void* stopEvent = nullptr;
    std::wstring fileName;
    std::function<void()> onChanged;
    std::jthread thread;
};

ConfigWatcher::ConfigWatcher() : impl_(std::make_unique<Impl>()) {}

ConfigWatcher::~ConfigWatcher() {
    Stop();
}

bool ConfigWatcher::Watch(const std::filesystem::path& configPath,
                          std::function<void()> onChanged) {
    Stop();

    if (!configPath.has_parent_path()) {
        return false;
    }

    const HANDLE directory = CreateFileW(
        configPath.parent_path().c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
        LogFormat(LogLevel::Warning,
                  L"Config watcher: cannot open \"%ls\" (error %lu).",
                  configPath.parent_path().c_str(), GetLastError());
        return false;
    }

    const HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) {
        CloseHandle(directory);
        return false;
    }

    impl_->directory = directory;
    impl_->stopEvent = stopEvent;
    impl_->fileName = configPath.filename().wstring();
    impl_->onChanged = std::move(onChanged);
    impl_->thread = std::jthread([this](std::stop_token token) { Run(token); });
    return true;
}

void ConfigWatcher::Stop() {
    if (!impl_) {
        return;
    }

    if (impl_->thread.joinable()) {
        impl_->thread.request_stop();
        if (impl_->stopEvent != nullptr) {
            SetEvent(static_cast<HANDLE>(impl_->stopEvent));
        }
        impl_->thread.join();
    }

    if (impl_->stopEvent != nullptr) {
        CloseHandle(static_cast<HANDLE>(impl_->stopEvent));
        impl_->stopEvent = nullptr;
    }
    if (impl_->directory != nullptr) {
        CloseHandle(static_cast<HANDLE>(impl_->directory));
        impl_->directory = nullptr;
    }
    impl_->onChanged = nullptr;
}

void ConfigWatcher::Run(std::stop_token token) {
    const HANDLE directory = static_cast<HANDLE>(impl_->directory);
    const HANDLE stopEvent = static_cast<HANDLE>(impl_->stopEvent);
    const std::wstring fileName = impl_->fileName;

    // Wakes the blocking ReadDirectoryChangesW when the thread is stopped.
    std::stop_callback wake(token, [stopEvent] { SetEvent(stopEvent); });

    const HANDLE changeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (changeEvent == nullptr) {
        return;
    }

    std::vector<BYTE> buffer(8 * 1024);
    OVERLAPPED overlapped{};
    overlapped.hEvent = changeEvent;

    const HANDLE handles[2] = {stopEvent, changeEvent};

    auto arm = [&]() -> bool {
        ResetEvent(changeEvent);
        if (ReadDirectoryChangesW(
                directory, buffer.data(), static_cast<DWORD>(buffer.size()), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                    FILE_NOTIFY_CHANGE_FILE_NAME,
                nullptr, &overlapped, nullptr)) {
            return true;
        }
        return GetLastError() == ERROR_IO_PENDING;
    };

    auto consume = [&](bool& matched) -> bool {
        DWORD transferred = 0;
        if (!GetOverlappedResult(directory, &overlapped, &transferred, FALSE)) {
            return GetLastError() == ERROR_OPERATION_ABORTED;
        }
        // A notification overflow means changes were dropped; treat it as a
        // change rather than ignoring the save.
        if (transferred == 0 || NotifyMatches(buffer, transferred, fileName)) {
            matched = true;
        }
        return true;
    };

    // One reload per burst: a notification only fires the callback after a
    // quiet period, and the pending read is always re-armed so no event is
    // missed while waiting.
    bool matched = false;
    bool running = arm();
    while (running && !token.stop_requested()) {
        const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, kDebounceMs);
        if (wait == WAIT_TIMEOUT) {
            if (matched) {
                matched = false;
                if (impl_->onChanged) {
                    impl_->onChanged();
                }
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            break; // Stop requested, or the wait failed.
        }
        running = consume(matched) && arm();
    }

    CancelIoEx(directory, &overlapped);
    CloseHandle(changeEvent);
}

} // namespace st
