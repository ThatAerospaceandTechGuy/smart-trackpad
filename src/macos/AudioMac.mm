#include "AudioMac.h"

#include "Log.h"

#include <CoreAudio/CoreAudio.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace st {
namespace {

constexpr AudioObjectPropertyElement kMainElement = kAudioObjectPropertyElementMain;

bool GetUInt32(AudioObjectID object, const AudioObjectPropertyAddress& address,
               UInt32* value) {
    UInt32 size = sizeof(*value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, value) == noErr;
}

bool GetFloat32(AudioObjectID object, const AudioObjectPropertyAddress& address,
                Float32* value) {
    UInt32 size = sizeof(*value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, value) == noErr;
}

bool SetFloat32(AudioObjectID object, const AudioObjectPropertyAddress& address,
                Float32 value) {
    return AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(value),
                                      &value) == noErr;
}

bool SetUInt32(AudioObjectID object, const AudioObjectPropertyAddress& address,
               UInt32 value) {
    return AudioObjectSetPropertyData(object, &address, 0, nullptr, sizeof(value),
                                      &value) == noErr;
}

} // namespace

CoreAudioVolume::CoreAudioVolume(LevelFeedbackFn feedback)
    : feedback_(std::move(feedback)) {}

CoreAudioVolume::~CoreAudioVolume() = default;

bool CoreAudioVolume::Initialize() {
    if (!ResolveDevice()) {
        LogMessage(LogLevel::Warning,
                   L"Volume: no default output device; the volume edge will have no "
                   L"effect until one exists.");
        return false;
    }

    available_ = true;
    LogMessage(LogLevel::Info, L"Volume: using Core Audio (default output device).");
    return true;
}

bool CoreAudioVolume::ResolveDevice() {
    AudioObjectPropertyAddress address{kAudioHardwarePropertyDefaultOutputDevice,
                                       kAudioObjectPropertyScopeGlobal,
                                       kMainElement};
    AudioObjectID device = 0;
    if (!GetUInt32(kAudioObjectSystemObject, address, &device) || device == 0) {
        device_ = 0;
        available_ = false;
        return false;
    }
    device_ = device;
    available_ = true;
    return true;
}

void CoreAudioVolume::SetTargetPercent(int percent) {
    if (!ResolveDevice()) {
        return;
    }

    const Float32 scalar = std::clamp(static_cast<Float32>(percent) / 100.0f, 0.0f, 1.0f);

    // Prefer the main element; many devices expose per-channel volume only.
    AudioObjectPropertyAddress volume{kAudioDevicePropertyVolumeScalar,
                                      kAudioDevicePropertyScopeOutput, kMainElement};
    if (!SetFloat32(device_, volume, scalar)) {
        for (const UInt32 channel : {1u, 2u}) {
            volume.mElement = channel;
            SetFloat32(device_, volume, scalar);
        }
    }

    Report();
}

void CoreAudioVolume::ToggleMute() {
    if (!ResolveDevice()) {
        return;
    }

    AudioObjectPropertyAddress mute{kAudioDevicePropertyMute,
                                    kAudioDevicePropertyScopeOutput, kMainElement};

    UInt32 current = 0;
    if (!GetUInt32(device_, mute, &current)) {
        // No master mute: emulate it by remembering the level and zeroing it.
        Float32 level = 1.0f;
        AudioObjectPropertyAddress volume{kAudioDevicePropertyVolumeScalar,
                                          kAudioDevicePropertyScopeOutput, kMainElement};
        if (!GetFloat32(device_, volume, &level)) {
            return;
        }
        if (level > 0.001f) {
            SetFloat32(device_, volume, 0.0f);
        } else {
            SetFloat32(device_, volume, 1.0f);
        }
        Report();
        return;
    }

    SetUInt32(device_, mute, current == 0 ? 1u : 0u);
    Report();
}

bool CoreAudioVolume::ReadLevel(int* percent, bool* muted) const {
    if (device_ == 0) {
        return false;
    }

    Float32 scalar = 0.0f;
    AudioObjectPropertyAddress volume{kAudioDevicePropertyVolumeScalar,
                                      kAudioDevicePropertyScopeOutput, kMainElement};
    if (!GetFloat32(device_, volume, &scalar)) {
        return false;
    }

    *percent = static_cast<int>(std::lround(scalar * 100.0f));

    UInt32 muteValue = 0;
    AudioObjectPropertyAddress mute{kAudioDevicePropertyMute,
                                    kAudioDevicePropertyScopeOutput, kMainElement};
    *muted = GetUInt32(device_, mute, &muteValue) && muteValue != 0;
    return true;
}

void CoreAudioVolume::Report() {
    int percent = 0;
    bool muted = false;
    if (feedback_ && ReadLevel(&percent, &muted)) {
        feedback_(LevelFeedback{percent, muted});
    }
}

} // namespace st
