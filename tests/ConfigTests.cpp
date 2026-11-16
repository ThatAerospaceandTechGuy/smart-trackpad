#include "Config.h"

#include <windows.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace {

int g_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": CHECK failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

bool NearlyEqual(float left, float right) {
    return std::fabs(left - right) < 1e-4f;
}

// The configuration format is UTF-16 with a BOM so GetPrivateProfile* reads it.
void WriteIni(const std::filesystem::path& path, const std::wstring& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const unsigned char bom[] = {0xFF, 0xFE};
    file.write(reinterpret_cast<const char*>(bom), sizeof(bom));
    file.write(reinterpret_cast<const char*>(text.data()),
               static_cast<std::streamsize>(text.size() * sizeof(wchar_t)));
}

bool HasSection(const std::filesystem::path& path, const wchar_t* section) {
    wchar_t buffer[16]{};
    return GetPrivateProfileStringW(section, nullptr, L"", buffer,
                                    static_cast<DWORD>(std::size(buffer)),
                                    path.c_str()) > 0;
}

class TempDirectory {
public:
    TempDirectory() {
        wchar_t buffer[MAX_PATH]{};
        const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
        std::wstring path(buffer, length);
        path += L"SmartTrackpadTests-";
        path += std::to_wstring(GetCurrentProcessId());
        path += L"-";
        path += std::to_wstring(GetTickCount64());
        directory_ = path;
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] std::filesystem::path File(const wchar_t* name) const {
        return directory_ / name;
    }

    [[nodiscard]] const std::filesystem::path& Path() const { return directory_; }

private:
    std::filesystem::path directory_;
};

void TestDefaultsAreCreated() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(std::filesystem::exists(path));
    CHECK(NearlyEqual(config.gestures.leftBand, 0.10f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.10f));
    CHECK(NearlyEqual(config.gestures.topBand, 0.10f));
    CHECK(NearlyEqual(config.gestures.activationDistance, 0.02f));
    CHECK(NearlyEqual(config.gestures.stepDistance, 0.10f));
    CHECK(config.gestures.leftEnabled);
    CHECK(config.gestures.rightEnabled);
    CHECK(config.gestures.topEnabled);
    CHECK(!config.gestures.invertVolume);
    CHECK(!config.gestures.invertBrightness);
    CHECK(!config.gestures.invertMedia);
    CHECK(!config.debugLogging);
    CHECK(config.gestures.tapsEnabled);
    CHECK(config.gestures.cooldownMs == 150);
    CHECK(config.gestures.minVolumePercent == 0);
    CHECK(config.gestures.maxVolumePercent == 100);
    CHECK(config.gestures.minBrightnessPercent == 5);   // safety floor
    CHECK(config.gestures.maxBrightnessPercent == 100);
    CHECK(config.devices == st::DigitizerDevices::Touchpad);
    CHECK(config.brightnessTargets == st::BrightnessTargets::All);
    CHECK(config.osdShowPercentage);
    CHECK(config.startWithWindows);
    CHECK(config.configPath == path);

    // The generated file must describe the current schema.
    CHECK(!HasSection(path, L"brightness"));
    CHECK(HasSection(path, L"zones"));
    CHECK(HasSection(path, L"gestures"));
    CHECK(HasSection(path, L"input"));
}

void TestValuesAndClamping() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([zones]
left_width=20
right_width=20
top_height=99
enable_left=yes
enable_right=off
enable_top=1

[gestures]
activation=50
step=0
invert_volume=1
invert_brightness=true
invert_media=on

[input]
debug=1
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(NearlyEqual(config.gestures.leftBand, 0.20f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.20f));
    CHECK(NearlyEqual(config.gestures.topBand, 0.45f));       // clamped down
    CHECK(NearlyEqual(config.gestures.activationDistance, 0.10f)); // clamped
    CHECK(NearlyEqual(config.gestures.stepDistance, 0.02f));       // clamped up
    CHECK(config.gestures.leftEnabled);
    CHECK(!config.gestures.rightEnabled);
    CHECK(config.gestures.topEnabled);
    CHECK(config.gestures.invertVolume);
    CHECK(config.gestures.invertBrightness);
    CHECK(config.gestures.invertMedia);
    CHECK(config.debugLogging);
}

void TestBandsAreClampedAndNeverOverlap() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([zones]
left_width=45
right_width=45
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    // Both bands are capped at 45% so they can never overlap.
    CHECK(NearlyEqual(config.gestures.leftBand, 0.45f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.45f));
    CHECK(config.gestures.leftBand + config.gestures.rightBand <= 0.9001f);
}

void TestMinimumBandsSurviveLeavingDefaults() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([zones]
left_width=0
right_width=0
top_height=0
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(NearlyEqual(config.gestures.leftBand, 0.01f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.01f));
    CHECK(NearlyEqual(config.gestures.topBand, 0.01f));
}

void TestLegacyConfigIsMigrated() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI(; Smart Trackpad configuration
[zones]
left_width=20
right_width=15
top_height=5
enable_left=1
enable_right=0
enable_top=1

[gestures]
activation=3.0
step=12.0
invert_volume=0
invert_brightness=1
invert_media=0

[brightness]
step_percent=5

[input]
debug=0
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    // The obsolete section is gone and the comments now describe the current
    // behaviour, while every user value survives the rewrite.
    CHECK(!HasSection(path, L"brightness"));
    CHECK(HasSection(path, L"zones"));
    CHECK(NearlyEqual(config.gestures.leftBand, 0.20f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.15f));
    CHECK(NearlyEqual(config.gestures.topBand, 0.05f));
    CHECK(config.gestures.leftEnabled);
    CHECK(!config.gestures.rightEnabled);
    CHECK(config.gestures.topEnabled);
    CHECK(NearlyEqual(config.gestures.activationDistance, 0.03f));
    CHECK(NearlyEqual(config.gestures.stepDistance, 0.12f));
    CHECK(config.gestures.invertBrightness);
    CHECK(!config.debugLogging);

    // A second load is stable: no further rewrite, same values.
    const st::AppConfig again = st::LoadConfig(path);
    CHECK(NearlyEqual(again.gestures.leftBand, config.gestures.leftBand));
    CHECK(NearlyEqual(again.gestures.activationDistance,
                      config.gestures.activationDistance));
}

void TestMalformedValuesFallBackToDefaults() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([zones]
left_width=abc
right_width=
top_height=-40

[gestures]
activation=
step=not-a-number

[input]
debug=maybe
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(NearlyEqual(config.gestures.leftBand, 0.10f));
    CHECK(NearlyEqual(config.gestures.rightBand, 0.10f));
    CHECK(NearlyEqual(config.gestures.topBand, 0.01f)); // negative clamps to min
    CHECK(NearlyEqual(config.gestures.activationDistance, 0.02f));
    CHECK(NearlyEqual(config.gestures.stepDistance, 0.10f));
    CHECK(!config.debugLogging);
}

void TestNewSectionsAreParsed() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([gestures]
taps=0
cooldown=500

[safety]
min_volume=15
max_volume=85
min_brightness=25
max_brightness=95

[display]
brightness_targets=primary

[input]
devices=touchscreen

[osd]
show_percentage=0

[general]
start_with_windows=0
config_version=2
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(!config.gestures.tapsEnabled);
    CHECK(config.gestures.cooldownMs == 500);
    CHECK(config.gestures.minVolumePercent == 15);
    CHECK(config.gestures.maxVolumePercent == 85);
    CHECK(config.gestures.minBrightnessPercent == 25);
    CHECK(config.gestures.maxBrightnessPercent == 95);
    CHECK(config.devices == st::DigitizerDevices::Touchscreen);
    CHECK(config.brightnessTargets == st::BrightnessTargets::Primary);
    CHECK(!config.osdShowPercentage);
    CHECK(!config.startWithWindows);
}

void TestInvertedSafetyRangeIsSwapped() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");
    WriteIni(path, LR"INI([safety]
min_volume=90
max_volume=10
)INI");

    const st::AppConfig config = st::LoadConfig(path);

    CHECK(config.gestures.minVolumePercent == 10);
    CHECK(config.gestures.maxVolumePercent == 90);
}

void TestSaveConfigRoundTrip() {
    TempDirectory temp;
    const std::filesystem::path path = temp.File(L"SmartTrackpad.ini");

    st::AppConfig config = st::LoadConfig(path);
    config.gestures.leftBand = 0.18f;
    config.gestures.topBand = 0.07f;
    config.gestures.stepDistance = 0.20f;
    config.gestures.activationDistance = 0.03f;
    config.gestures.invertMedia = true;
    config.gestures.tapsEnabled = false;
    config.gestures.cooldownMs = 400;
    config.gestures.minBrightnessPercent = 20;
    config.gestures.maxVolumePercent = 90;
    config.devices = st::DigitizerDevices::Both;
    config.brightnessTargets = st::BrightnessTargets::Internal;
    config.osdShowPercentage = false;
    config.startWithWindows = false;
    config.debugLogging = true;

    CHECK(st::SaveConfig(path, config));

    const st::AppConfig reloaded = st::LoadConfig(path);
    CHECK(NearlyEqual(reloaded.gestures.leftBand, 0.18f));
    CHECK(NearlyEqual(reloaded.gestures.topBand, 0.07f));
    CHECK(NearlyEqual(reloaded.gestures.stepDistance, 0.20f));
    CHECK(NearlyEqual(reloaded.gestures.activationDistance, 0.03f));
    CHECK(reloaded.gestures.invertMedia);
    CHECK(!reloaded.gestures.tapsEnabled);
    CHECK(reloaded.gestures.cooldownMs == 400);
    CHECK(reloaded.gestures.minBrightnessPercent == 20);
    CHECK(reloaded.gestures.maxVolumePercent == 90);
    CHECK(reloaded.devices == st::DigitizerDevices::Both);
    CHECK(reloaded.brightnessTargets == st::BrightnessTargets::Internal);
    CHECK(!reloaded.osdShowPercentage);
    CHECK(!reloaded.startWithWindows);
    CHECK(reloaded.debugLogging);

    // The rewritten file is current: no legacy section, and a schema marker.
    CHECK(!HasSection(path, L"brightness"));
    CHECK(HasSection(path, L"safety"));
    CHECK(HasSection(path, L"general"));
}

} // namespace

#define RUN_TEST(test)              \
    do {                            \
        std::cout << #test << '\n'; \
        test();                     \
    } while (false)

int main() {
    RUN_TEST(TestDefaultsAreCreated);
    RUN_TEST(TestValuesAndClamping);
    RUN_TEST(TestBandsAreClampedAndNeverOverlap);
    RUN_TEST(TestMinimumBandsSurviveLeavingDefaults);
    RUN_TEST(TestLegacyConfigIsMigrated);
    RUN_TEST(TestMalformedValuesFallBackToDefaults);
    RUN_TEST(TestNewSectionsAreParsed);
    RUN_TEST(TestInvertedSafetyRangeIsSwapped);
    RUN_TEST(TestSaveConfigRoundTrip);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All configuration tests passed.\n";
    return 0;
}
