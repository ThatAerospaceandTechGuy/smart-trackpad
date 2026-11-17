#include "Volume.h"

#include "ComPtr.h"
#include "Log.h"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>

namespace st {

struct VolumeController::AudioState {
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> endpoint;
    bool warned = false;

    bool EnsureEndpoint();
    std::optional<LevelFeedback> Set(int percent);
    std::optional<LevelFeedback> ToggleMute();
    void Warn(const wchar_t* operation, HRESULT result);
};

void VolumeController::AudioState::Warn(const wchar_t* operation, HRESULT result) {
    if (warned) {
        return;
    }
    warned = true;
    LogFormat(LogLevel::Warning, L"Volume: %ls failed (0x%08lX).", operation,
              static_cast<unsigned long>(result));
}

bool VolumeController::AudioState::EnsureEndpoint() {
    if (endpoint) {
        return true;
    }

    if (!enumerator) {
        const HRESULT result =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                             IID_PPV_ARGS(enumerator.Put()));
        if (FAILED(result) || !enumerator) {
            Warn(L"CoCreateInstance(MMDeviceEnumerator)", result);
            return false;
        }
    }

    if (!device) {
        const HRESULT result =
            enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.Put());
        if (FAILED(result) || !device) {
            Warn(L"GetDefaultAudioEndpoint", result);
            return false;
        }
    }

    if (!endpoint) {
        const HRESULT result = device->Activate(
            __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(endpoint.Put()));
        if (FAILED(result) || !endpoint) {
            Warn(L"Activate(IAudioEndpointVolume)", result);
            return false;
        }
    }

    return true;
}

std::optional<LevelFeedback> VolumeController::AudioState::Set(int percent) {
    if (!EnsureEndpoint()) {
        return std::nullopt;
    }

    const float scalar = static_cast<float>(std::clamp(percent, 0, 100)) / 100.0f;
    HRESULT result = endpoint->SetMasterVolumeLevelScalar(scalar, nullptr);

    if (FAILED(result)) {
        // The default output device may have changed or been removed.  Drop
        // the cached endpoint and try once with the current default.
        endpoint.Reset();
        device.Reset();
        if (!EnsureEndpoint()) {
            return std::nullopt;
        }
        result = endpoint->SetMasterVolumeLevelScalar(scalar, nullptr);
    }

    if (FAILED(result)) {
        Warn(L"SetMasterVolumeLevelScalar", result);
        return std::nullopt;
    }

    warned = false;
    LogFormat(LogLevel::Debug, L"Volume: set %d%%.", percent);

    // Read it back: Core Audio may quantize the value to the endpoint's step
    // size, and the endpoint may be muted.  The overlay shows what the user
    // will actually hear rather than what was requested.
    LevelFeedback feedback;
    feedback.percent = std::clamp(percent, 0, 100);

    float readBack = 0.0f;
    if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&readBack))) {
        feedback.percent = std::clamp(
            static_cast<int>(std::lround(readBack * 100.0f)), 0, 100);
    }

    BOOL muted = FALSE;
    if (SUCCEEDED(endpoint->GetMute(&muted))) {
        feedback.muted = muted != FALSE;
    }

    if (feedback.percent != percent) {
        LogFormat(LogLevel::Debug,
                  L"Volume: endpoint quantized %d%% to %d%%.", percent,
                  feedback.percent);
    }

    return feedback;
}

std::optional<LevelFeedback> VolumeController::AudioState::ToggleMute() {
    if (!EnsureEndpoint()) {
        return std::nullopt;
    }

    BOOL muted = FALSE;
    if (FAILED(endpoint->GetMute(&muted))) {
        Warn(L"GetMute", E_FAIL);
        return std::nullopt;
    }

    const HRESULT result = endpoint->SetMute(muted ? FALSE : TRUE, nullptr);
    if (FAILED(result)) {
        Warn(L"SetMute", result);
        return std::nullopt;
    }

    LevelFeedback feedback;
    float readBack = 0.0f;
    if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&readBack))) {
        feedback.percent = std::clamp(
            static_cast<int>(std::lround(readBack * 100.0f)), 0, 100);
    }

    BOOL nowMuted = FALSE;
    if (SUCCEEDED(endpoint->GetMute(&nowMuted))) {
        feedback.muted = nowMuted != FALSE;
    }

    LogFormat(LogLevel::Debug, L"Volume: %ls.", feedback.muted ? L"muted" : L"unmuted");
    return feedback;
}

VolumeController::VolumeController(LevelFeedbackFn onApplied)
    : onApplied_(std::move(onApplied)) {
    worker_ = std::jthread([this](std::stop_token stop) { Run(std::move(stop)); });
}

VolumeController::~VolumeController() {
    worker_.request_stop();
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void VolumeController::SetTargetPercent(int percent) {
    percent = std::clamp(percent, 0, 100);

    {
        std::lock_guard lock(mutex_);
        pendingPercent_ = percent;
    }
    condition_.notify_one();
}

void VolumeController::ToggleMute() {
    {
        std::lock_guard lock(mutex_);
        pendingMuteToggle_ = true;
    }
    condition_.notify_one();
}

void VolumeController::Run(std::stop_token stop) {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comReady =
        SUCCEEDED(comResult) || comResult == RPC_E_CHANGED_MODE;

    if (!comReady) {
        LogFormat(LogLevel::Warning,
                  L"Volume: CoInitializeEx failed (0x%08lX); volume control is "
                  L"disabled.",
                  static_cast<unsigned long>(comResult));
    } else {
        audio_ = std::make_unique<AudioState>();
    }

    {
        std::unique_lock lock(mutex_);

        for (;;) {
            condition_.wait(lock, [this, &stop] {
                return pendingPercent_ >= 0 || pendingMuteToggle_ ||
                       stop.stop_requested();
            });

            if (stop.stop_requested()) {
                break;
            }

            // Coalesce a fast slide into one Core Audio update.
            condition_.wait_for(lock, std::chrono::milliseconds(25));
            if (stop.stop_requested()) {
                break;
            }

            const int percent = std::exchange(pendingPercent_, -1);
            const bool toggle = std::exchange(pendingMuteToggle_, false);
            if (percent < 0 && !toggle) {
                continue;
            }

            lock.unlock();
            if (audio_) {
                const std::optional<LevelFeedback> feedback =
                    toggle ? audio_->ToggleMute() : audio_->Set(percent);
                if (feedback && onApplied_) {
                    onApplied_(*feedback);
                }
            }
            lock.lock();
        }
    }

    // Release Core Audio interfaces on the thread that created them, while
    // COM is still initialized.
    audio_.reset();
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
}

} // namespace st
