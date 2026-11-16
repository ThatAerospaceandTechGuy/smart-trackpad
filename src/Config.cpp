#include "Config.h"

#include "Log.h"

#include <windows.h>

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
    bool enableRight = true;
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
    // Write through a temporary file so a watcher or a crash never sees a
    // half-written INI, then replace the original.
    std::filesystem::path temporary = path;
    temporary += L".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        // GetPrivateProfile* understand UTF-16 INI files with a BOM.
        const unsigned char bom[] = {0xFF, 0xFE};
        file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
        file.write(reinterpret_cast<const char*>(text.data()),
                   static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
        file.flush();
        if (!file.good()) {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            return false;
        }
    }

    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        return false;
    }
    return true;
}

bool HasSection(const wchar_t* section, const std::filesystem::path& file) {
    wchar_t buffer[16]{};
    const DWORD copied =
        GetPrivateProfileStringW(section, nullptr, L"", buffer,
                                 static_cast<DWORD>(std::size(buffer)), file.c_str());
    return copied > 0;
}

float ClampValue(float value, float low, float high) {
    return std::clamp(value, low, high);
}

int ReadInt(const wchar_t* section, const wchar_t* key, int fallback,
            const std::filesystem::path& file) {
    // Parsed by hand rather than with GetPrivateProfileIntW so a malformed
    // value falls back to the documented default instead of silently becoming
    // zero.
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(section, key, L"", buffer,
                             static_cast<DWORD>(std::size(buffer)), file.c_str());
    if (buffer[0] == L'\0') {
        return fallback;
    }

    wchar_t* end = nullptr;
    const long value = std::wcstol(buffer, &end, 10);
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

std::wstring ReadString(const wchar_t* section, const wchar_t* key,
                        const wchar_t* fallback,
                        const std::filesystem::path& file) {
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(section, key, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), file.c_str());

    std::wstring value(buffer);
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
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
    gestures.rightEnabled = ReadBool(L"zones", L"enable_right", true, path);
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

// ---------------------------------------------------------------------------
// ConfigWatcher
// ---------------------------------------------------------------------------

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

ConfigWatcher::ConfigWatcher() = default;

ConfigWatcher::~ConfigWatcher() {
    Stop();
}

bool ConfigWatcher::Watch(const std::filesystem::path& configPath,
                          std::function<void()> onChanged) {
    Stop();

    const std::filesystem::path directory = configPath.parent_path();
    if (directory.empty()) {
        return false;
    }

    const HANDLE directoryHandle =
        CreateFileW(directory.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directoryHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    const HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) {
        CloseHandle(directoryHandle);
        return false;
    }

    directory_ = directoryHandle;
    stopEvent_ = stopEvent;
    fileName_ = configPath.filename().wstring();
    onChanged_ = std::move(onChanged);

    thread_ = std::jthread([this](std::stop_token token) { Run(std::move(token)); });
    return true;
}

void ConfigWatcher::Stop() {
    if (thread_.joinable()) {
        thread_.request_stop();
        if (stopEvent_ != nullptr) {
            SetEvent(static_cast<HANDLE>(stopEvent_));
        }
        thread_.join();
    }

    if (stopEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stopEvent_));
        stopEvent_ = nullptr;
    }
    if (directory_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(directory_));
        directory_ = nullptr;
    }
    onChanged_ = nullptr;
}

void ConfigWatcher::Run(std::stop_token token) {
    const HANDLE directory = static_cast<HANDLE>(directory_);
    const HANDLE stopEvent = static_cast<HANDLE>(stopEvent_);

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
        if (transferred == 0 || NotifyMatches(buffer, transferred, fileName_)) {
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
                if (onChanged_) {
                    onChanged_();
                }
            }
            continue;
        }
        if (wait != WAIT_OBJECT_0 + 1) {
            break;
        }
        if (!consume(matched)) {
            break;
        }
        running = arm();
    }

    CancelIoEx(directory, &overlapped);
    CloseHandle(changeEvent);
}

} // namespace st
