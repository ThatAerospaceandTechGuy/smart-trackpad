#include "Settings.h"

#include "Log.h"
#include "Resources.h"

#include <windows.h>
#include <commctrl.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <iterator>
#include <string>

namespace st {
namespace {

// Values shown in the dialog.  Percentages are stored as whole percents and the
// activation distance as tenths of a percent, which is what the sliders can
// express; the INI file remains the place for finer values.
struct DialogState {
    AppConfig* config = nullptr;

    int leftWidth = 10;
    int rightWidth = 10;
    int topHeight = 10;
    int activationTenths = 20;
    int stepPercent = 10;
    int cooldownMs = 150;
    int minVolume = 0;
    int maxVolume = 100;
    int minBrightness = 5;
    int maxBrightness = 100;

    bool leftEnabled = true;
    bool rightEnabled = true;
    bool topEnabled = true;
    bool taps = true;
    bool invertVolume = false;
    bool invertBrightness = false;
    bool invertMedia = false;
    bool showPercentage = true;
    bool startWithWindows = true;
    bool debug = false;

    int devices = 0;
    int targets = 0;

    bool accepted = false;
};

struct SliderBinding {
    int controlId;
    int labelId;
    int minimum;
    int maximum;
    int DialogState::*field;
    bool tenths;
    const wchar_t* unit;
};

constexpr SliderBinding kSliders[] = {
    {IDC_SLIDER_LEFT_W, IDC_VALUE_LEFT_W, 1, 45, &DialogState::leftWidth, false, L"%" },
    {IDC_SLIDER_RIGHT_W, IDC_VALUE_RIGHT_W, 1, 45, &DialogState::rightWidth, false, L"%" },
    {IDC_SLIDER_TOP_H, IDC_VALUE_TOP_H, 1, 45, &DialogState::topHeight, false, L"%" },
    {IDC_SLIDER_ACTIVATION, IDC_VALUE_ACTIVATION, 2, 100, &DialogState::activationTenths,
     true, L"%" },
    {IDC_SLIDER_STEP, IDC_VALUE_STEP, 2, 50, &DialogState::stepPercent, false, L"%" },
    {IDC_SLIDER_COOLDOWN, IDC_VALUE_COOLDOWN, 0, 1000, &DialogState::cooldownMs, false,
     L" ms" },
    {IDC_SLIDER_MIN_BRIGHTNESS, IDC_VALUE_MIN_BRIGHTNESS, 0, 100,
     &DialogState::minBrightness, false, L"%" },
    {IDC_SLIDER_MAX_BRIGHTNESS, IDC_VALUE_MAX_BRIGHTNESS, 0, 100,
     &DialogState::maxBrightness, false, L"%" },
    {IDC_SLIDER_MIN_VOLUME, IDC_VALUE_MIN_VOLUME, 0, 100, &DialogState::minVolume, false,
     L"%" },
    {IDC_SLIDER_MAX_VOLUME, IDC_VALUE_MAX_VOLUME, 0, 100, &DialogState::maxVolume, false,
     L"%" },
};

struct CheckBinding {
    int controlId;
    bool DialogState::*field;
};

constexpr CheckBinding kChecks[] = {
    {IDC_CHECK_LEFT, &DialogState::leftEnabled},
    {IDC_CHECK_RIGHT, &DialogState::rightEnabled},
    {IDC_CHECK_TOP, &DialogState::topEnabled},
    {IDC_CHECK_TAPS, &DialogState::taps},
    {IDC_CHECK_INVERT_VOLUME, &DialogState::invertVolume},
    {IDC_CHECK_INVERT_BRIGHTNESS, &DialogState::invertBrightness},
    {IDC_CHECK_INVERT_MEDIA, &DialogState::invertMedia},
    {IDC_CHECK_SHOW_PERCENTAGE, &DialogState::showPercentage},
    {IDC_CHECK_START_WITH_WINDOWS, &DialogState::startWithWindows},
    {IDC_CHECK_DEBUG, &DialogState::debug},
};

void LoadFromConfig(DialogState& state) {
    const GestureSettings& gestures = state.config->gestures;
    state.leftWidth = static_cast<int>(gestures.leftBand * 100.0f + 0.5f);
    state.rightWidth = static_cast<int>(gestures.rightBand * 100.0f + 0.5f);
    state.topHeight = static_cast<int>(gestures.topBand * 100.0f + 0.5f);
    state.activationTenths =
        std::clamp(static_cast<int>(gestures.activationDistance * 1000.0f + 0.5f), 2, 100);
    state.stepPercent =
        std::clamp(static_cast<int>(gestures.stepDistance * 100.0f + 0.5f), 2, 50);
    state.cooldownMs = std::clamp(gestures.cooldownMs, 0, 1000);
    state.minVolume = gestures.minVolumePercent;
    state.maxVolume = gestures.maxVolumePercent;
    state.minBrightness = gestures.minBrightnessPercent;
    state.maxBrightness = gestures.maxBrightnessPercent;

    state.leftEnabled = gestures.leftEnabled;
    state.rightEnabled = gestures.rightEnabled;
    state.topEnabled = gestures.topEnabled;
    state.taps = gestures.tapsEnabled;
    state.invertVolume = gestures.invertVolume;
    state.invertBrightness = gestures.invertBrightness;
    state.invertMedia = gestures.invertMedia;

    state.showPercentage = state.config->osdShowPercentage;
    state.startWithWindows = state.config->startWithWindows;
    state.debug = state.config->debugLogging;

    switch (state.config->devices) {
    case DigitizerDevices::Touchscreen:
        state.devices = 1;
        break;
    case DigitizerDevices::Both:
        state.devices = 2;
        break;
    default:
        state.devices = 0;
        break;
    }

    switch (state.config->brightnessTargets) {
    case BrightnessTargets::Primary:
        state.targets = 1;
        break;
    case BrightnessTargets::Internal:
        state.targets = 2;
        break;
    default:
        state.targets = 0;
        break;
    }
}

void ApplyDefaults(DialogState& state) {
    const AppConfig defaults;
    state.leftWidth = 10;
    state.rightWidth = 10;
    state.topHeight = 10;
    state.activationTenths = 20;
    state.stepPercent = 10;
    state.cooldownMs = defaults.gestures.cooldownMs;
    state.minVolume = defaults.gestures.minVolumePercent;
    state.maxVolume = defaults.gestures.maxVolumePercent;
    state.minBrightness = defaults.gestures.minBrightnessPercent;
    state.maxBrightness = defaults.gestures.maxBrightnessPercent;
    state.leftEnabled = true;
    state.rightEnabled = true;
    state.topEnabled = true;
    state.taps = true;
    state.invertVolume = false;
    state.invertBrightness = false;
    state.invertMedia = false;
    state.showPercentage = true;
    state.startWithWindows = true;
    state.debug = false;
    state.devices = 0;
    state.targets = 0;
}

std::wstring FormatValue(const SliderBinding& binding, int value) {
    wchar_t buffer[64]{};
    if (binding.tenths) {
        swprintf(buffer, std::size(buffer), L"%.1f%ls",
                 static_cast<double>(value) / 10.0, binding.unit);
    } else {
        swprintf(buffer, std::size(buffer), L"%d%ls", value, binding.unit);
    }
    return buffer;
}

void UpdateLabels(HWND dialog, const DialogState& state) {
    for (const SliderBinding& binding : kSliders) {
        SetDlgItemTextW(dialog, binding.labelId,
                        FormatValue(binding, state.*(binding.field)).c_str());
    }
}

void PushStateToControls(HWND dialog, const DialogState& state) {
    for (const SliderBinding& binding : kSliders) {
        const HWND slider = GetDlgItem(dialog, binding.controlId);
        SendMessageW(slider, TBM_SETRANGE, TRUE,
                     MAKELPARAM(binding.minimum, binding.maximum));
        // A tick for every point turns the track into a dotted line.
        const int span = binding.maximum - binding.minimum;
        SendMessageW(slider, TBM_SETTICFREQ, std::max(1, span / 10), 0);
        SendMessageW(slider, TBM_SETPOS, TRUE, state.*(binding.field));
    }

    for (const CheckBinding& binding : kChecks) {
        CheckDlgButton(dialog, binding.controlId,
                       state.*(binding.field) ? BST_CHECKED : BST_UNCHECKED);
    }

    SendDlgItemMessageW(dialog, IDC_COMBO_TARGETS, CB_SETCURSEL, state.targets, 0);
    SendDlgItemMessageW(dialog, IDC_COMBO_DEVICES, CB_SETCURSEL, state.devices, 0);

    UpdateLabels(dialog, state);
}

void PullControls(HWND dialog, DialogState& state) {
    for (const SliderBinding& binding : kSliders) {
        const LRESULT position =
            SendDlgItemMessageW(dialog, binding.controlId, TBM_GETPOS, 0, 0);
        state.*(binding.field) = static_cast<int>(position);
    }

    for (const CheckBinding& binding : kChecks) {
        state.*(binding.field) =
            IsDlgButtonChecked(dialog, binding.controlId) == BST_CHECKED;
    }

    const LRESULT targets =
        SendDlgItemMessageW(dialog, IDC_COMBO_TARGETS, CB_GETCURSEL, 0, 0);
    const LRESULT devices =
        SendDlgItemMessageW(dialog, IDC_COMBO_DEVICES, CB_GETCURSEL, 0, 0);
    state.targets = targets == CB_ERR ? 0 : static_cast<int>(targets);
    state.devices = devices == CB_ERR ? 0 : static_cast<int>(devices);
}

void StoreToConfig(const DialogState& state) {
    AppConfig& config = *state.config;
    GestureSettings& gestures = config.gestures;

    gestures.leftBand = static_cast<float>(state.leftWidth) / 100.0f;
    gestures.rightBand = static_cast<float>(state.rightWidth) / 100.0f;
    gestures.topBand = static_cast<float>(state.topHeight) / 100.0f;
    gestures.activationDistance = static_cast<float>(state.activationTenths) / 1000.0f;
    gestures.stepDistance = static_cast<float>(state.stepPercent) / 100.0f;
    gestures.cooldownMs = state.cooldownMs;

    gestures.minVolumePercent = std::min(state.minVolume, state.maxVolume);
    gestures.maxVolumePercent = std::max(state.minVolume, state.maxVolume);
    gestures.minBrightnessPercent = std::min(state.minBrightness, state.maxBrightness);
    gestures.maxBrightnessPercent = std::max(state.minBrightness, state.maxBrightness);

    gestures.leftEnabled = state.leftEnabled;
    gestures.rightEnabled = state.rightEnabled;
    gestures.topEnabled = state.topEnabled;
    gestures.tapsEnabled = state.taps;
    gestures.invertVolume = state.invertVolume;
    gestures.invertBrightness = state.invertBrightness;
    gestures.invertMedia = state.invertMedia;

    config.osdShowPercentage = state.showPercentage;
    config.startWithWindows = state.startWithWindows;
    config.debugLogging = state.debug;

    switch (state.devices) {
    case 1:
        config.devices = DigitizerDevices::Touchscreen;
        break;
    case 2:
        config.devices = DigitizerDevices::Both;
        break;
    default:
        config.devices = DigitizerDevices::Touchpad;
        break;
    }

    switch (state.targets) {
    case 1:
        config.brightnessTargets = BrightnessTargets::Primary;
        break;
    case 2:
        config.brightnessTargets = BrightnessTargets::Internal;
        break;
    default:
        config.brightnessTargets = BrightnessTargets::All;
        break;
    }
}

void CenterOnCursorMonitor(HWND dialog) {
    RECT window{};
    if (!GetWindowRect(dialog, &window)) {
        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }

    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int left = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    const int top = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;

    SetWindowPos(dialog, nullptr, left, top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<DialogState*>(
        GetWindowLongPtrW(dialog, GWLP_USERDATA));

    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, lParam);
        if (state == nullptr) {
            return FALSE;
        }

        SendDlgItemMessageW(dialog, IDC_COMBO_TARGETS, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"All displays"));
        SendDlgItemMessageW(dialog, IDC_COMBO_TARGETS, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Primary only"));
        SendDlgItemMessageW(dialog, IDC_COMBO_TARGETS, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Internal panel"));
        SendDlgItemMessageW(dialog, IDC_COMBO_DEVICES, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Touchpad"));
        SendDlgItemMessageW(dialog, IDC_COMBO_DEVICES, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Touchscreen"));
        SendDlgItemMessageW(dialog, IDC_COMBO_DEVICES, CB_ADDSTRING, 0,
                            reinterpret_cast<LPARAM>(L"Both"));

        PushStateToControls(dialog, *state);
        SendMessageW(dialog, WM_SETICON, ICON_SMALL,
                     reinterpret_cast<LPARAM>(
                         LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON))));
        CenterOnCursorMonitor(dialog);
        return TRUE;
    }

    case WM_HSCROLL:
        if (state != nullptr) {
            PullControls(dialog, *state);
            UpdateLabels(dialog, *state);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_DEFAULTS:
            if (state != nullptr) {
                ApplyDefaults(*state);
                PushStateToControls(dialog, *state);
            }
            return TRUE;

        case IDOK:
            if (state != nullptr) {
                PullControls(dialog, *state);
                StoreToConfig(*state);
                state->accepted = true;
            }
            EndDialog(dialog, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        default:
            break;
        }
        return FALSE;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    default:
        break;
    }

    return FALSE;
}

} // namespace

bool ShowSettingsDialog(HINSTANCE instance, HWND owner, AppConfig& config) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    DialogState state;
    state.config = &config;
    LoadFromConfig(state);

    const INT_PTR result = DialogBoxParamW(
        instance, MAKEINTRESOURCEW(IDD_SETTINGS), owner, &DialogProc,
        reinterpret_cast<LPARAM>(&state));

    if (result == -1) {
        LogFormat(LogLevel::Warning,
                  L"Settings: the dialog could not be created (error %lu).",
                  GetLastError());
        return false;
    }

    return state.accepted;
}

} // namespace st
