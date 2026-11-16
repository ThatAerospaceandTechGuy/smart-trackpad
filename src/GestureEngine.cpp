#include "GestureEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace st {
namespace {

constexpr float kMinStep = 0.005f;
constexpr float kMinActivation = 0.001f;
constexpr int kMaxMediaStepsPerEvent = 12;

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SanitizeStep(float value) {
    return std::clamp(value, kMinStep, 0.5f);
}

float SanitizeActivation(float value) {
    return std::clamp(value, kMinActivation, 0.5f);
}

std::uint64_t MonotonicMilliseconds() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

int SanitizePercent(int value) {
    return std::clamp(value, 0, 100);
}

GestureSettings SanitizeSettings(GestureSettings settings) {
    // Degenerate distances would make the step loop spin forever.
    settings.stepDistance = SanitizeStep(settings.stepDistance);
    settings.activationDistance = SanitizeActivation(settings.activationDistance);
    settings.tapDistance = Clamp01(std::max(settings.tapDistance, 0.001f));
    settings.tapDurationMs = std::clamp(settings.tapDurationMs, 50, 2000);
    settings.cooldownMs = std::clamp(settings.cooldownMs, 0, 2000);

    settings.minVolumePercent = SanitizePercent(settings.minVolumePercent);
    settings.maxVolumePercent = SanitizePercent(settings.maxVolumePercent);
    settings.minBrightnessPercent = SanitizePercent(settings.minBrightnessPercent);
    settings.maxBrightnessPercent = SanitizePercent(settings.maxBrightnessPercent);
    if (settings.minVolumePercent > settings.maxVolumePercent) {
        std::swap(settings.minVolumePercent, settings.maxVolumePercent);
    }
    if (settings.minBrightnessPercent > settings.maxBrightnessPercent) {
        std::swap(settings.minBrightnessPercent, settings.maxBrightnessPercent);
    }
    return settings;
}

} // namespace

GestureEngine::GestureEngine(GestureSettings settings, VolumeFn onVolume,
                             ActionFn onAction, BrightnessFn onBrightness,
                             ClockFn clock)
    : settings_(SanitizeSettings(settings)),
      onVolume_(std::move(onVolume)),
      onAction_(std::move(onAction)),
      onBrightness_(std::move(onBrightness)),
      clock_(std::move(clock)) {}

void GestureEngine::SetSettings(const GestureSettings& settings) {
    settings_ = SanitizeSettings(settings);
    Finish();
}

std::uint64_t GestureEngine::Now() const {
    return clock_ ? clock_() : MonotonicMilliseconds();
}

bool GestureEngine::IsCoolingDown() const noexcept {
    return Now() < cooldownUntilMs_;
}

void GestureEngine::OnContactDown(float x, float y) {
    // A second contact while a gesture is in flight is an input-layer error;
    // ignore it rather than restarting the gesture under the user's finger.
    if (state_ != State::Idle) {
        return;
    }

    // A cancelled gesture (second finger, device removed) leaves the pad hot;
    // wait out the cooldown so the palm that caused it cannot re-trigger.
    if (Now() < cooldownUntilMs_) {
        return;
    }

    state_ = State::Pending;
    zone_ = EdgeZone::None;
    start_ = Point{Clamp01(x), Clamp01(y)};
    anchor_ = start_;
    downTimeMs_ = Now();
    lastVolumePercent_ = -1;
    lastBrightnessPercent_ = -1;
}

void GestureEngine::OnContactMove(float x, float y) {
    const Point current{Clamp01(x), Clamp01(y)};

    if (state_ == State::Pending) {
        const float dx = current.x - start_.x;
        const float dy = current.y - start_.y;

        if (std::max(std::abs(dx), std::abs(dy)) < settings_.activationDistance) {
            return;
        }

        zone_ = ResolveZone(dx, dy);
        if (zone_ == EdgeZone::None) {
            // Movement did not match any zone (or matched the wrong axis).
            // Stay blocked until the finger is lifted.
            state_ = State::Blocked;
            return;
        }

        state_ = State::Active;
        // The travel that activated a top-edge gesture is intentionally
        // discarded so the first media step needs a full stepDistance of
        // horizontal movement.  Volume and brightness are absolute, so they
        // immediately follow the contact's Y position.
        anchor_ = current;
        if (zone_ == EdgeZone::Left) {
            EmitVolume(current.y);
        } else if (zone_ == EdgeZone::Right) {
            EmitBrightness(current.y);
        }
        return;
    }

    if (state_ != State::Active) {
        return; // Blocked or Idle: nothing to do.
    }

    if (zone_ == EdgeZone::Left) {
        // Absolute volume slider: bottom of the pad = 0%, top = 100%.
        EmitVolume(current.y);
        return;
    }

    if (zone_ == EdgeZone::Right) {
        // Absolute brightness slider: bottom of the pad = 0%, top = 100%.
        EmitBrightness(current.y);
        return;
    }

    if (zone_ != EdgeZone::Top) {
        return;
    }

    const float delta = current.x - anchor_.x;
    int steps = static_cast<int>(delta / settings_.stepDistance);
    if (steps == 0) {
        return;
    }

    // A single HID report can jump a long way; cap the burst so a glitch
    // cannot turn into a hundred media actions.  Unconsumed travel remains
    // in anchor_ and is picked up by the next report.
    steps = std::clamp(steps, -kMaxMediaStepsPerEvent, kMaxMediaStepsPerEvent);

    const int direction = (steps > 0) ? 1 : -1;
    for (int i = 0; i < std::abs(steps); ++i) {
        EmitMediaStep(direction);
    }

    anchor_.x += static_cast<float>(steps) * settings_.stepDistance;
}

void GestureEngine::OnContactUp() {
    // A contact that never travelled far enough to activate a zone is a tap,
    // provided it was short and started inside an enabled band.
    if (state_ == State::Pending && settings_.tapsEnabled) {
        const std::uint64_t held = Now() - downTimeMs_;
        if (held <= static_cast<std::uint64_t>(settings_.tapDurationMs)) {
            const EdgeZone tapZone = ResolveTapZone();
            if (tapZone != EdgeZone::None) {
                EmitTap(tapZone);
            }
        }
    }

    Finish();
}

void GestureEngine::OnContactCancel() {
    StartCooldown(Now());
    Finish();
}

void GestureEngine::Reset() {
    Finish();
}

EdgeZone GestureEngine::ResolveZone(float dx, float dy) const {
    const bool inLeft = settings_.leftEnabled && start_.x <= settings_.leftBand;
    const bool inRight = settings_.rightEnabled && start_.x >= 1.0f - settings_.rightBand;
    const bool inTop = settings_.topEnabled && start_.y >= 1.0f - settings_.topBand;

    // In a corner both a vertical edge and the top edge match.  The dominant
    // motion decides: vertical -> volume/brightness, horizontal -> media.
    if (std::abs(dx) > std::abs(dy)) {
        return inTop ? EdgeZone::Top : EdgeZone::None;
    }

    if (inLeft) {
        return EdgeZone::Left;
    }
    if (inRight) {
        return EdgeZone::Right;
    }
    return EdgeZone::None;
}

EdgeZone GestureEngine::ResolveTapZone() const {
    // A tap carries no direction, so a corner resolves to the vertical edge:
    // the sliders are the controls a tap augments.
    if (settings_.leftEnabled && start_.x <= settings_.leftBand) {
        return EdgeZone::Left;
    }
    if (settings_.rightEnabled && start_.x >= 1.0f - settings_.rightBand) {
        return EdgeZone::Right;
    }
    if (settings_.topEnabled && start_.y >= 1.0f - settings_.topBand) {
        return EdgeZone::Top;
    }
    return EdgeZone::None;
}

void GestureEngine::EmitMediaStep(int direction) {
    EdgeAction action =
        direction > 0 ? EdgeAction::MediaNext : EdgeAction::MediaPrevious;
    if (settings_.invertMedia) {
        action = (action == EdgeAction::MediaNext) ? EdgeAction::MediaPrevious
                                                   : EdgeAction::MediaNext;
    }

    if (onAction_) {
        onAction_(action);
    }
}

void GestureEngine::EmitTap(EdgeZone zone) {
    if (!onAction_) {
        return;
    }

    switch (zone) {
    case EdgeZone::Left:
        onAction_(EdgeAction::ToggleMute);
        break;
    case EdgeZone::Right:
        onAction_(EdgeAction::MonitorOff);
        break;
    case EdgeZone::Top:
        onAction_(EdgeAction::MediaPlayPause);
        break;
    case EdgeZone::None:
        break;
    }
}

void GestureEngine::EmitVolume(float y) {
    if (!onVolume_ || !settings_.leftEnabled) {
        return;
    }

    int percent = static_cast<int>(std::lround(Clamp01(y) * 100.0f));
    percent = std::clamp(percent, 0, 100);
    if (settings_.invertVolume) {
        percent = 100 - percent;
    }
    percent = std::clamp(percent, settings_.minVolumePercent,
                         settings_.maxVolumePercent);

    // Reports arrive as fast as the HID device produces them.  Only forward
    // a change of at least one percent; the volume worker coalesces the rest.
    if (percent == lastVolumePercent_) {
        return;
    }

    lastVolumePercent_ = percent;
    onVolume_(percent);
}

void GestureEngine::EmitBrightness(float y) {
    if (!onBrightness_ || !settings_.rightEnabled) {
        return;
    }

    int percent = static_cast<int>(std::lround(Clamp01(y) * 100.0f));
    percent = std::clamp(percent, 0, 100);
    if (settings_.invertBrightness) {
        percent = 100 - percent;
    }
    percent = std::clamp(percent, settings_.minBrightnessPercent,
                         settings_.maxBrightnessPercent);

    // Gesture reports arrive as fast as the HID device produces them.  Only
    // forward a change of at least one percent; the brightness worker
    // coalesces the rest.
    if (percent == lastBrightnessPercent_) {
        return;
    }

    lastBrightnessPercent_ = percent;
    onBrightness_(percent);
}

void GestureEngine::StartCooldown(std::uint64_t now) {
    if (settings_.cooldownMs > 0) {
        cooldownUntilMs_ = now + static_cast<std::uint64_t>(settings_.cooldownMs);
    }
}

void GestureEngine::Finish() {
    state_ = State::Idle;
    zone_ = EdgeZone::None;
    start_ = Point{};
    anchor_ = Point{};
    downTimeMs_ = 0;
    lastVolumePercent_ = -1;
    lastBrightnessPercent_ = -1;
}

} // namespace st
