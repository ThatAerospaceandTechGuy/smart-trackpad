#include "Config.h"

#include "IniFile.h"
#include "Log.h"
#include "Platform.h"
#include "Text.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace st {
namespace {

// ---------------------------------------------------------------------------
// Default / canonical configuration text.
//
// The same generator is used for the first-run file, for the one-time
// migration of a release that still had a [brightness] section, and for the
// settings dialog.  Every comment therefore always describes the current
// behaviour.
// ---------------------------------------------------------------------------

constexpr int kConfigVersion = 2;

struct ConfigText {
    int leftWidth = 10;
    int rightWidth = 10;
    int topHeight = 10;
    float activation = 2.0f;
    float step = 10.0f;
    bool enableLeft = true;
    bool enableRight = false;
    bool enableTop = true;
    bool invertVolume = false;
    bool invertBrightness = false;
    bool invertMedia = false;
    bool taps = true;
    int cooldown = 150;
    int minVolume = 0;
    int maxVolume = 100;
    int minBrightness = 5;
    int maxBrightness = 100;
    DigitizerDevices devices = DigitizerDevices::Touchpad;
    BrightnessTargets targets = BrightnessTargets::All;
    bool showPercentage = true;
    bool startWithWindows = true;
    bool debug = false;
};

const wchar_t* DevicesToText(DigitizerDevices devices) {
    switch (devices) {
    case DigitizerDevices::Touchscreen:
        return L"touchscreen";
    case DigitizerDevices::Both:
        return L"both";
    case DigitizerDevices::Touchpad:
        break;
    }
    return L"touchpad";
}

const wchar_t* TargetsToText(BrightnessTargets targets) {
    switch (targets) {
    case BrightnessTargets::Primary:
        return L"primary";
    case BrightnessTargets::Internal:
        return L"internal";
    case BrightnessTargets::All:
        break;
    }
    return L"all";
}

std::wstring BuildConfigText(const ConfigText& values) {
    std::wstring text;

    auto line = [&text](std::wstring value) {
        text += value;
        text += L"\r\n";
    };
    auto number = [](int value) { return std::to_wstring(value); };
    auto decimal = [](float value) {
        wchar_t buffer[32]{};
        swprintf(buffer, std::size(buffer), L"%.1f", static_cast<double>(value));
        return std::wstring(buffer);
    };
    auto flag = [](bool value) { return std::wstring(value ? L"1" : L"0"); };

    line(L"; Smart Trackpad configuration");
    line(L";");
    line(L"; All zone sizes and gesture distances are percentages of the touchpad");
    line(L"; surface.  Vertical coordinates run bottom (0%) to top (100%).");
    line(L"; The left and right edges are absolute sliders (bottom = 0%, top = 100%);");
    line(L"; the top edge is step based.  Save this file to apply the changes: the");
    line(L"; running instance reloads it automatically.");
    line(L"");
    line(L"[zones]");
    line(L"; Width of the left and right vertical edge bands, in percent (1-45).");
    line(L"left_width=" + number(values.leftWidth));
    line(L"right_width=" + number(values.rightWidth));
    line(L"; Height of the top horizontal edge band, in percent (1-45).");
    line(L"top_height=" + number(values.topHeight));
    line(L"enable_left=" + flag(values.enableLeft));
    line(L"enable_right=" + flag(values.enableRight));
    line(L"enable_top=" + flag(values.enableTop));
    line(L"");
    line(L"[gestures]");
    line(L"; Movement required before an edge gesture is recognized, in percent");
    line(L"; (0.2-10).");
    line(L"activation=" + decimal(values.activation));
    line(L"; Horizontal travel per media step on the top edge, in percent (2-50).");
    line(L"; The left and right edges are absolute and do not use this value.");
    line(L"step=" + decimal(values.step));
    line(L"; Tap the edge instead of sliding: left = mute, top = play/pause,");
    line(L"; right = turn the display off.");
    line(L"taps=" + flag(values.taps));
    line(L"; Pause (milliseconds, 0-2000) before a gesture is accepted again after");
    line(L"; one was cancelled by a second finger.");
    line(L"cooldown=" + number(values.cooldown));
    line(L"; Set to 1 to reverse an axis (100% at the bottom instead of the top).");
    line(L"invert_volume=" + flag(values.invertVolume));
    line(L"invert_brightness=" + flag(values.invertBrightness));
    line(L"invert_media=" + flag(values.invertMedia));
    line(L"");
    line(L"[safety]");
    line(L"; Output range clamps in percent.  Keep a brightness floor: many panels");
    line(L"; go completely black at 0.");
    line(L"min_volume=" + number(values.minVolume));
    line(L"max_volume=" + number(values.maxVolume));
    line(L"min_brightness=" + number(values.minBrightness));
    line(L"max_brightness=" + number(values.maxBrightness));
    line(L"");
    line(L"[display]");
    line(L"; Which displays the brightness edge drives: all, primary or internal.");
    line(L"brightness_targets=" + std::wstring(TargetsToText(values.targets)));
    line(L"");
    line(L"[input]");
    line(L"; Which digitizer to listen to: touchpad, touchscreen or both.");
    line(L"devices=" + std::wstring(DevicesToText(values.devices)));
    line(L"; Set to 1 to log raw device information and per-action diagnostics.");
    line(L"debug=" + flag(values.debug));
    line(L"");
    line(L"[osd]");
    line(L"; Show the numeric percentage next to the level bar.");
    line(L"show_percentage=" + flag(values.showPercentage));
    line(L"");
    line(L"[general]");
    line(L"; Start the managed copy when you sign in.");
    line(L"start_with_windows=" + flag(values.startWithWindows));
    line(L"; Written by the program; used to migrate older files.");
    line(L"config_version=" + number(kConfigVersion));

    return text;
}

ConfigText ToConfigText(const AppConfig& config) {
    const GestureSettings& gestures = config.gestures;
    ConfigText values;
    values.leftWidth = static_cast<int>(gestures.leftBand * 100.0f + 0.5f);
    values.rightWidth = static_cast<int>(gestures.rightBand * 100.0f + 0.5f);
    values.topHeight = static_cast<int>(gestures.topBand * 100.0f + 0.5f);
    values.activation = gestures.activationDistance * 100.0f;
    values.step = gestures.stepDistance * 100.0f;
    values.enableLeft = gestures.leftEnabled;
    values.enableRight = gestures.rightEnabled;
    values.enableTop = gestures.topEnabled;
    values.invertVolume = gestures.invertVolume;
    values.invertBrightness = gestures.invertBrightness;
    values.invertMedia = gestures.invertMedia;
    values.taps = gestures.tapsEnabled;
    values.cooldown = gestures.cooldownMs;
    values.minVolume = gestures.minVolumePercent;
    values.maxVolume = gestures.maxVolumePercent;
    values.minBrightness = gestures.minBrightnessPercent;
    values.maxBrightness = gestures.maxBrightnessPercent;
    values.devices = config.devices;
    values.targets = config.brightnessTargets;
    values.showPercentage = config.osdShowPercentage;
    values.startWithWindows = config.startWithWindows;
    values.debug = config.debugLogging;
    return values;
}

bool WriteConfigFile(const std::filesystem::path& path, const std::wstring& text) {
    return WriteUtf8FileAtomic(path, text);
}

bool HasSection(const wchar_t* section, const std::filesystem::path& file) {
    return HasProfileSection(section, file);
}

float ClampValue(float value, float low, float high) {
    return std::clamp(value, low, high);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback,
            const std::filesystem::path& file) {
    // Parsed by hand rather than with GetPrivateProfileIntW so a malformed
    // value falls back to the documented default instead of silently becoming
    // zero.
    const std::wstring buffer = ReadProfileString(section, key, L"", file);
    if (buffer[0] == L'\0') {
        return fallback;
    }

    wchar_t* end = nullptr;
    const long value = std::wcstol(buffer.c_str(), &end, 10);
    if (end == buffer) {
        return fallback;
    }
    while (*end != L'\0' && std::iswspace(*end) != 0) {
        ++end;
    }
    if (*end != L'\0') {
        return fallback;
    }

    return static_cast<int>(std::clamp<long>(value, -1000, 1000));
}

float ReadFloat(const wchar_t* section, const wchar_t* key, float fallback,
                const std::filesystem::path& file) {
    const std::wstring buffer = ReadProfileString(section, key, L"", file);
    if (buffer[0] == L'\0') {
        return fallback;
    }

    wchar_t* end = nullptr;
    const float value = std::wcstof(buffer.c_str(), &end);
    return (end == buffer) ? fallback : value;
}

std::wstring ReadString(const wchar_t* section, const wchar_t* key,
                        const wchar_t* fallback,
                        const std::filesystem::path& file) {
    std::wstring value = ReadProfileString(section, key, fallback, file);
    value = text::LowerAscii(value);
    return value;
}

bool ReadBool(const wchar_t* section, const wchar_t* key, bool fallback,
              const std::filesystem::path& file) {
    const wchar_t* defaultValue = fallback ? L"1" : L"0";
    const std::wstring buffer = ReadProfileString(section, key, defaultValue, file);

    std::wstring value = text::LowerAscii(buffer);

    return value == L"1" || value == L"true" || value == L"yes" || value == L"on";
}

DigitizerDevices ReadDevices(const std::filesystem::path& file) {
    const std::wstring value = ReadString(L"input", L"devices", L"touchpad", file);
    if (value == L"touchscreen" || value == L"screen" || value == L"2") {
        return DigitizerDevices::Touchscreen;
    }
    if (value == L"both" || value == L"all" || value == L"3") {
        return DigitizerDevices::Both;
    }
    return DigitizerDevices::Touchpad;
}

BrightnessTargets ReadTargets(const std::filesystem::path& file) {
    const std::wstring value =
        ReadString(L"display", L"brightness_targets", L"all", file);
    if (value == L"primary" || value == L"1") {
        return BrightnessTargets::Primary;
    }
    if (value == L"internal" || value == L"laptop" || value == L"2") {
        return BrightnessTargets::Internal;
    }
    return BrightnessTargets::All;
}

void CreateDefaultConfigIfMissing(const std::filesystem::path& path) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    if (error || std::filesystem::exists(path, error)) {
        return;
    }

    WriteConfigFile(path, BuildConfigText(ConfigText{}));
}

AppConfig ParseConfig(const std::filesystem::path& path) {
    AppConfig parsed;
    parsed.configPath = path;

    GestureSettings& gestures = parsed.gestures;

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

    gestures.tapsEnabled = ReadBool(L"gestures", L"taps", true, path);
    gestures.cooldownMs = std::clamp(ReadInt(L"gestures", L"cooldown", 150, path),
                                     0, 2000);

    gestures.leftEnabled = ReadBool(L"zones", L"enable_left", true, path);
    // Off by default: an absolute brightness mapping is surprising when a
    // fingertip touches the bottom of the right edge and the panel goes dark.
    gestures.rightEnabled = ReadBool(L"zones", L"enable_right", false, path);
    gestures.topEnabled = ReadBool(L"zones", L"enable_top", true, path);

    gestures.invertVolume = ReadBool(L"gestures", L"invert_volume", false, path);
    gestures.invertBrightness = ReadBool(L"gestures", L"invert_brightness", false, path);
    gestures.invertMedia = ReadBool(L"gestures", L"invert_media", false, path);

    gestures.minVolumePercent = std::clamp(ReadInt(L"safety", L"min_volume", 0, path),
                                           0, 100);
    gestures.maxVolumePercent =
        std::clamp(ReadInt(L"safety", L"max_volume", 100, path), 0, 100);
    gestures.minBrightnessPercent =
        std::clamp(ReadInt(L"safety", L"min_brightness", 5, path), 0, 100);
    gestures.maxBrightnessPercent =
        std::clamp(ReadInt(L"safety", L"max_brightness", 100, path), 0, 100);
    if (gestures.minVolumePercent > gestures.maxVolumePercent) {
        std::swap(gestures.minVolumePercent, gestures.maxVolumePercent);
    }
    if (gestures.minBrightnessPercent > gestures.maxBrightnessPercent) {
        std::swap(gestures.minBrightnessPercent, gestures.maxBrightnessPercent);
    }

    parsed.devices = ReadDevices(path);
    parsed.brightnessTargets = ReadTargets(path);
    parsed.osdShowPercentage = ReadBool(L"osd", L"show_percentage", true, path);
    parsed.startWithWindows = ReadBool(L"general", L"start_with_windows", true, path);
    parsed.debugLogging = ReadBool(L"input", L"debug", false, path);
    return parsed;
}

} // namespace

AppConfig LoadConfig(const std::filesystem::path& path) {
    CreateDefaultConfigIfMissing(path);

    AppConfig config = ParseConfig(path);

    // Releases before 1.1.0 stored a [brightness] step size that no longer
    // exists.  Rewrite such a file with the current template so its comments
    // match what the program actually does; the parsed values are preserved.
    // The file is parsed again so the rewritten file is the single source of
    // truth for clamping (the rewrite rounds band sizes to whole percents).
    if (HasSection(L"brightness", path) &&
        WriteConfigFile(path, BuildConfigText(ToConfigText(config)))) {
        config = ParseConfig(path);
        LogFormat(LogLevel::Info,
                  L"Configuration: migrated \"%ls\" to the current format "
                  L"(the obsolete [brightness] section was removed).",
                  path.c_str());
    }

    return config;
}

bool SaveConfig(const std::filesystem::path& path, const AppConfig& config) {
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
    }

    const bool written = WriteConfigFile(path, BuildConfigText(ToConfigText(config)));
    if (written) {
        LogFormat(LogLevel::Info, L"Configuration: saved \"%ls\".", path.c_str());
    } else {
        LogFormat(LogLevel::Warning, L"Configuration: could not save \"%ls\".",
                  path.c_str());
    }
    return written;
}

} // namespace st
