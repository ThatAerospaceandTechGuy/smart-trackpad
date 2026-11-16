#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace st {
namespace {

std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
bool g_debugEnabled = false;
std::filesystem::path g_logPath;
unsigned long long g_bytesOnDisk = 0;

// Rotate once the file grows past this size.  Rotation happens while the log
// mutex is held, so a long-running session cannot exceed the limit by more
// than one line.
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

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

    const int byteCount = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0,
                                              nullptr, nullptr);
    if (byteCount <= 0) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        utf8.data(), byteCount, nullptr, nullptr);
    return utf8;
}

// Opens the log file in append mode and records the current size so rotation
// can happen without a separate stat call.  Caller holds g_logMutex.
void OpenLogFile() {
    g_logFile = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    g_bytesOnDisk = 0;
    if (g_logFile == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(g_logFile, &size) && size.QuadPart > 0) {
        g_bytesOnDisk = static_cast<unsigned long long>(size.QuadPart);
    }
}

// Closes the current file, moves it aside and starts a fresh one.
// Caller holds g_logMutex.
void RotateLogFile() {
    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }

    std::filesystem::path backup = g_logPath;
    backup += L".old";
    MoveFileExW(g_logPath.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);

    OpenLogFile();
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

    OpenLogFile();

    // A log left over from a previous run may already be oversized.
    if (g_bytesOnDisk > kMaxLogSize) {
        RotateLogFile();
    }
}

void ShutdownLogging() {
    std::lock_guard lock(g_logMutex);

    if (g_logFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
    g_bytesOnDisk = 0;
}

void LogMessage(LogLevel level, const std::wstring& message) {
    std::lock_guard lock(g_logMutex);

    if (level == LogLevel::Debug && !g_debugEnabled) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t prefix[96]{};
    swprintf(prefix, std::size(prefix), L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%ls] ",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
             now.wMilliseconds, LevelTag(level));

    const std::wstring line = std::wstring(prefix) + message + L"\r\n";

    OutputDebugStringW(line.c_str());

    if (g_logFile == INVALID_HANDLE_VALUE) {
        return;
    }

    const std::string utf8 = ToUtf8(line);
    if (utf8.empty()) {
        return;
    }

    if (g_bytesOnDisk > 0 && g_bytesOnDisk + utf8.size() > kMaxLogSize) {
        RotateLogFile();
    }

    DWORD written = 0;
    if (WriteFile(g_logFile, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr)) {
        g_bytesOnDisk += written;
    }
}

void LogFormat(LogLevel level, const wchar_t* format, ...) {
    std::wstring message;

#if defined(_MSC_VER)
    va_list arguments;
    va_start(arguments, format);
    va_list sizing;
    va_copy(sizing, arguments);
    const int needed = _vscwprintf(format, sizing);
    va_end(sizing);

    if (needed < 0) {
        message = format;
    } else {
        std::vector<wchar_t> buffer(static_cast<std::size_t>(needed) + 1, L'\0');
        _vsnwprintf_s(buffer.data(), buffer.size(), _TRUNCATE, format, arguments);
        message.assign(buffer.data());
    }
    va_end(arguments);
#else
    std::vector<wchar_t> buffer(4096, L'\0');
    va_list arguments;
    va_start(arguments, format);
    std::vswprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    message.assign(buffer.data());
#endif

    LogMessage(level, message);
}

std::filesystem::path LogPath() {
    std::lock_guard lock(g_logMutex);
    return g_logPath;
}

} // namespace st
