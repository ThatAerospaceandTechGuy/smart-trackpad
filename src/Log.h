#pragma once

#include <filesystem>
#include <string>

namespace st {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

void InitLogging(const std::filesystem::path& logPath, bool debugEnabled);
void ShutdownLogging();

void LogMessage(LogLevel level, const std::wstring& message);
void LogFormat(LogLevel level, const wchar_t* format, ...);

[[nodiscard]] std::filesystem::path LogPath();

} // namespace st
