#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace st {
namespace {

constexpr const wchar_t* kDefaultIni = LR"INI(; Smart Trackpad configuration
; Zone sizes and gesture distances are percentages of the touchpad surface.
; Vertical coordinates run bottom (0%) to top (100%).
; The left and right edges are absolute sliders: bottom = 0%, top = 100%.
; Restart the application after changing this file.

[zones]
; Width of the left and right vertical edge bands, in percent.
left_width=10
right_width=10
; Height of the top horizontal edge band, in percent.
top_height=10
enable_left=1
enable_right=1
enable_top=1

[gestures]
; Movement required before an edge gesture is recognized, in percent.
activation=2.0
; Horizontal travel per media step, in percent of the touchpad.
; The left and right edges are absolute and do not use this value.
step=10.0
invert_volume=0
invert_brightness=0
invert_media=0

[input]
; Set to 1 to log raw device information and per-action diagnostics.
debug=0
)INI";

float ClampValue(float value, float low, float high) {
    return std::clamp(value, low, high);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback,
            const std::filesystem::path& file) {
    return GetPrivateProfileIntW(section, key, fallback, file.c_str());
}

float ReadFloat(const wchar_t* section, const wchar_t* key, float fallback,
                const std::filesystem::path& file) {
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(section, key, L"", buffer,
                             static_cast<DWORD>(std::size(buffer)), file.c_str());
    if (buffer[0] == L'\0') {
        return fallback;
    }

    wchar_t* end = nullptr;
    const float value = std::wcstof(buffer, &end);
    return (end == buffer) ? fallback : value;
}

bool ReadBool(const wchar_t* section, const wchar_t* key, bool fallback,
              const std::filesystem::path& file) {
    const wchar_t* defaultValue = fallback ? L"1" : L"0";
    wchar_t buffer[16]{};
    GetPrivateProfileStringW(section, key, defaultValue, buffer,
                             static_cast<DWORD>(std::size(buffer)), file.c_str());

    std::wstring value(buffer);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });

    return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
}

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');

    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }

    return std::filesystem::path(buffer).parent_path();
}

void CreateDefaultConfigIfMissing(const std::filesystem::path& path) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error || std::filesystem::exists(path, error)) {
        return;
    }

    // GetPrivateProfileStringW understands UTF-16 INI files with a BOM.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return;
    }

    const unsigned char bom[] = {0xFF, 0xFE};
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));

    const std::wstring text(kDefaultIni);
    file.write(reinterpret_cast<const char*>(text.data()),
               static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
}

} // namespace

std::filesystem::path GetDataDirectory() {
    const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (required > 0) {
        std::wstring value(required, L'\0');
        const DWORD written =
            GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), required);
        if (written > 0 && written < required) {
            value.resize(written);
            return std::filesystem::path(value) / L"SmartTrackpad";
        }
    }

    // Portable fallback: keep user data next to the executable.
    const std::filesystem::path directory = ExecutableDirectory();
    if (!directory.empty()) {
        return directory / L"SmartTrackpad";
    }
    return std::filesystem::path(L"SmartTrackpad");
}

std::filesystem::path GetConfigPath() {
    return GetDataDirectory() / L"SmartTrackpad.ini";
}

AppConfig LoadConfig(const std::filesystem::path& path) {
    CreateDefaultConfigIfMissing(path);

    AppConfig config;
    config.configPath = path;

    GestureSettings& gestures = config.gestures;

    const int leftWidth = ReadInt(L"zones", L"left_width", 10, path);
    const int rightWidth = ReadInt(L"zones", L"right_width", 10, path);
    const int topHeight = ReadInt(L"zones", L"top_height", 10, path);

    gestures.leftBand = ClampValue(static_cast<float>(leftWidth) / 100.0f, 0.01f, 0.45f);
    gestures.rightBand = ClampValue(static_cast<float>(rightWidth) / 100.0f, 0.01f, 0.45f);
    gestures.topBand = ClampValue(static_cast<float>(topHeight) / 100.0f, 0.01f, 0.45f);

    // Keep the two vertical bands from overlapping; left wins.
    if (gestures.leftBand + gestures.rightBand > 0.90f) {
        gestures.rightBand = 0.90f - gestures.leftBand;
    }

    gestures.activationDistance =
        ClampValue(ReadFloat(L"gestures", L"activation", 2.0f, path) / 100.0f,
                   0.002f, 0.10f);
    gestures.stepDistance =
        ClampValue(ReadFloat(L"gestures", L"step", 10.0f, path) / 100.0f,
                   0.02f, 0.50f);

    gestures.leftEnabled = ReadBool(L"zones", L"enable_left", true, path);
    gestures.rightEnabled = ReadBool(L"zones", L"enable_right", true, path);
    gestures.topEnabled = ReadBool(L"zones", L"enable_top", true, path);

    gestures.invertVolume = ReadBool(L"gestures", L"invert_volume", false, path);
    gestures.invertBrightness = ReadBool(L"gestures", L"invert_brightness", false, path);
    gestures.invertMedia = ReadBool(L"gestures", L"invert_media", false, path);

    config.debugLogging = ReadBool(L"input", L"debug", false, path);

    return config;
}

} // namespace st
