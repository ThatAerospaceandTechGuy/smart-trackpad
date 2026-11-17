#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

#include "Platform.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

namespace st {
namespace {

std::filesystem::path CleanUp(std::filesystem::path path) {
    std::error_code error;
    path = std::filesystem::absolute(path, error);
    return error ? std::filesystem::path{} : path.lexically_normal();
}

} // namespace

std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (written < buffer.size()) {
            buffer.resize(written);
            return CleanUp(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return CleanUp(buffer.data());
#else
    std::vector<char> buffer(1024, '\0');
    for (;;) {
        const ssize_t written =
            readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (written <= 0) {
            return {};
        }
        if (static_cast<std::size_t>(written) < buffer.size() - 1) {
            buffer.resize(static_cast<std::size_t>(written));
            return CleanUp(buffer.data());
        }
        buffer.resize(buffer.size() * 2);
        buffer.back() = '\0';
    }
#endif
}

std::filesystem::path ExecutableDirectory() {
    const std::filesystem::path executable = ExecutablePath();
    return executable.empty() ? std::filesystem::path{} : executable.parent_path();
}

std::filesystem::path GetDataDirectory() {
#if defined(_WIN32)
    const wchar_t* base = _wgetenv(L"LOCALAPPDATA");
    if (base != nullptr && base[0] != L'\0') {
        return CleanUp(std::filesystem::path(base) / L"SmartTrackpad");
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return CleanUp(std::filesystem::path(home) / "Library" /
                       "Application Support" / "SmartTrackpad");
    }
#else
    if (const char* base = std::getenv("XDG_DATA_HOME");
        base != nullptr && base[0] != '\0') {
        return CleanUp(std::filesystem::path(base) / "SmartTrackpad");
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return CleanUp(std::filesystem::path(home) / ".local" / "share" /
                       "SmartTrackpad");
    }
#endif

    // Portable fallback: keep user data next to the executable.
    const std::filesystem::path directory = ExecutableDirectory();
    if (!directory.empty()) {
        return directory / "SmartTrackpad";
    }
    return std::filesystem::path("SmartTrackpad");
}

std::filesystem::path GetConfigPath() {
    return GetDataDirectory() / L"SmartTrackpad.ini";
}

bool HasInteractiveSession() {
#if defined(_WIN32)
    return true;
#elif defined(__APPLE__)
    return true;
#else
    // A bare SSH session has no audio server or input devices to talk to.
    const char* display = std::getenv("WAYLAND_DISPLAY");
    if (display != nullptr && display[0] != '\0') {
        return true;
    }
    display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
#endif
}

} // namespace st
