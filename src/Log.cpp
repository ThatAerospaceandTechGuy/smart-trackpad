#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>

namespace st {
namespace {

std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
bool g_debugEnabled = false;
std::filesystem::path g_logPath;

constexpr unsigned long long kMaxLogSize = 1024ull * 1024ull;

const wchar_t* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return L"DBG";
    case LogLevel::Info:
        return L"INF";
    case LogLevel::Warning:
        return L"WRN";
    case LogLevel::Error:
        return L"ERR";
    }
    return L"???";
}

void WriteUtf8(HANDLE file, const std::wstring& text) {
    if (text.empty()) {
        return;
    }

    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0,
                                              nullptr, nullptr);
    if (byteCount <= 0) {
        return;
    }

    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), byteCount, nullptr, nullptr);

    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void RotateIfNeeded(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return;
    }

    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size <= kMaxLogSize) {
        return;
    }

    std::filesystem::path backup = path;
    backup += L".old";
    MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace

void InitLogging(const std::filesystem::path& logPath, bool debugEnabled) {
    std::lock_guard lock(g_logMutex);

    g_debugEnabled = debugEnabled;
    g_logPath = logPath;

    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }

    if (logPath.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(logPath.parent_path(), error);
    }

    RotateIfNeeded(logPath);

    g_logFile = CreateFileW(logPath.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void ShutdownLogging() {
    std::lock_guard lock(g_logMutex);

    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void LogMessage(LogLevel level, const std::wstring& message) {
    std::lock_guard lock(g_logMutex);

    if (level == LogLevel::Debug && !g_debugEnabled) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t prefix[96]{};
    swprintf(prefix, std::size(prefix),
             L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%ls] ", now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
             LevelTag(level));

    const std::wstring line = std::wstring(prefix) + message + L"\r\n";

    OutputDebugStringW(line.c_str());

    if (g_logFile != INVALID_HANDLE_VALUE) {
        WriteUtf8(g_logFile, line);
    }
}

void LogFormat(LogLevel level, const wchar_t* format, ...) {
    wchar_t buffer[1024]{};

    va_list arguments;
    va_start(arguments, format);
#if defined(_MSC_VER)
    _vsnwprintf_s(buffer, std::size(buffer), _TRUNCATE, format, arguments);
#else
    std::vswprintf(buffer, std::size(buffer), format, arguments);
#endif
    va_end(arguments);

    LogMessage(level, buffer);
}

std::filesystem::path LogPath() {
    std::lock_guard lock(g_logMutex);
    return g_logPath;
}

} // namespace st
