#include "Log.h"

#include "Text.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdio>
#include <cstdlib>
#endif

#include <chrono>
#include <cstdarg>
#include <cwchar>
#include <ctime>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace st {
namespace {

std::mutex g_logMutex;
std::FILE* g_logFile = nullptr;
bool g_debugEnabled = false;
bool g_echoToStderr = false;
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

// Opens the log file in append mode and records the current size so rotation
// can happen without a separate stat call.  Caller holds g_logMutex.
void OpenLogFile() {
    g_bytesOnDisk = 0;
#if defined(_WIN32)
    g_logFile = _wfopen(g_logPath.c_str(), L"ab");
#else
    g_logFile = std::fopen(g_logPath.c_str(), "ab");
#endif
    if (g_logFile == nullptr) {
        return;
    }

    std::error_code error;
    const auto size = std::filesystem::file_size(g_logPath, error);
    if (!error) {
        g_bytesOnDisk = static_cast<unsigned long long>(size);
    }
}

// Closes the current file, moves it aside and starts a fresh one.
// Caller holds g_logMutex.
void RotateLogFile() {
    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }

    std::filesystem::path backup = g_logPath;
    backup += L".old";

    std::error_code error;
    std::filesystem::remove(backup, error);
    std::filesystem::rename(g_logPath, backup, error);

    OpenLogFile();
}

} // namespace

void InitLogging(const std::filesystem::path& logPath, bool debugEnabled) {
    std::lock_guard lock(g_logMutex);

    g_debugEnabled = debugEnabled;
    g_logPath = logPath;

    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }

    if (logPath.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(logPath.parent_path(), error);
    }

#if !defined(_WIN32)
    if (const char* echo = std::getenv("SMARTTRACKPAD_ECHO"); echo != nullptr) {
        g_echoToStderr = echo[0] != '\0' && echo[0] != '0';
    }
#endif

    OpenLogFile();

    // A log left over from a previous run may already be oversized.
    if (g_bytesOnDisk > kMaxLogSize) {
        RotateLogFile();
    }
}

void ShutdownLogging() {
    std::lock_guard lock(g_logMutex);

    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    g_bytesOnDisk = 0;
}

namespace {

// Caller holds g_logMutex.
void WriteEntry(LogLevel level, const std::wstring& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count() %
                        1000;

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif

    wchar_t prefix[96]{};
    swprintf(prefix, std::size(prefix),
             L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%ls] ", local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
             static_cast<int>(millis), LevelTag(level));

    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    const std::string utf8 = text::ToUtf8(line);

#if defined(_WIN32)
    OutputDebugStringW(line.c_str());
#else
    if (g_echoToStderr) {
        std::fwrite(utf8.data(), 1, utf8.size(), stderr);
        std::fflush(stderr);
    }
#endif

    if (g_logFile == nullptr || utf8.empty()) {
        return;
    }

    if (g_bytesOnDisk > 0 && g_bytesOnDisk + utf8.size() > kMaxLogSize) {
        RotateLogFile();
        if (g_logFile == nullptr) {
            return;
        }
    }

    const std::size_t written = std::fwrite(utf8.data(), 1, utf8.size(), g_logFile);
    std::fflush(g_logFile);
    g_bytesOnDisk += written;
}

} // namespace

void LogMessage(LogLevel level, const std::wstring& message) {
    std::lock_guard lock(g_logMutex);

    if (level == LogLevel::Debug && !g_debugEnabled) {
        return;
    }

    WriteEntry(level, message);
}

void LogMessageBestEffort(LogLevel level, const std::wstring& message) {
    std::unique_lock lock(g_logMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        // The logger is busy, which is exactly what happens during a crash: put
        // the line somewhere else instead of blocking.
        std::wstring debugLine = message;
        debugLine.push_back(static_cast<wchar_t>(13));
        debugLine.push_back(static_cast<wchar_t>(10));
#if defined(_WIN32)
        OutputDebugStringW(debugLine.c_str());
#else
        const std::string utf8 = text::ToUtf8(debugLine);
        std::fwrite(utf8.data(), 1, utf8.size(), stderr);
#endif
        return;
    }

    if (level == LogLevel::Debug && !g_debugEnabled) {
        return;
    }

    WriteEntry(level, message);
}

void LogFormat(LogLevel level, const wchar_t* format, ...) {
    std::wstring message;

    va_list arguments;
    va_start(arguments, format);

    // vswprintf needs a bounded buffer, so grow until the result fits.
    std::vector<wchar_t> buffer(1024, L'\0');
    for (;;) {
        va_list attempt;
        va_copy(attempt, arguments);
        const int written = vswprintf(buffer.data(), buffer.size(), format, attempt);
        va_end(attempt);

        if (written >= 0) {
            message.assign(buffer.data(), static_cast<std::size_t>(written));
            break;
        }
        if (buffer.size() >= 64 * 1024) {
            message = format;
            break;
        }
        buffer.resize(buffer.size() * 2, L'\0');
    }

    va_end(arguments);

    LogMessage(level, message);
}

std::filesystem::path LogPath() {
    std::lock_guard lock(g_logMutex);
    return g_logPath;
}

} // namespace st
