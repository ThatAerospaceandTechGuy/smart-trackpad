#pragma once

#include <functional>

namespace st {

enum class EdgeAction {
    MediaNext,
    MediaPrevious,
};

enum class EdgeZone {
    None,
    Left,
    Right,
    Top,
};

// Coordinates passed to the engine are normalized to the touchpad surface:
//   x: 0.0 = left, 1.0 = right
//   y: 0.0 = bottom, 1.0 = top
// HID digitizers report Y downward, so RawInputSource flips it into this
// bottom-up space.  The left and right edges are absolute sliders; the top
// edge is step-based.
struct GestureSettings {
    float leftBand = 0.10f;              // left vertical edge: volume
    float rightBand = 0.10f;             // right vertical edge: brightness
    float topBand = 0.10f;               // top horizontal edge: media

    float activationDistance = 0.02f;    // travel before a zone is latched
    float stepDistance = 0.10f;          // horizontal travel per media step

    bool leftEnabled = true;
    bool rightEnabled = true;
    bool topEnabled = true;

    bool invertVolume = false;
    bool invertBrightness = false;
    bool invertMedia = false;
};

// Converts absolute contact positions into edge-slider values.  The class is
// deliberately independent of Win32 so the zone logic is unit-testable.
class GestureEngine {
public:
    using ActionFn = std::function<void(EdgeAction)>;
    using VolumeFn = std::function<void(int percent)>;
    using BrightnessFn = std::function<void(int percent)>;

    GestureEngine(GestureSettings settings, VolumeFn onVolume, ActionFn onAction,
                  BrightnessFn onBrightness);

    GestureEngine(const GestureEngine&) = delete;
    GestureEngine& operator=(const GestureEngine&) = delete;

    void SetSettings(const GestureSettings& settings);
    [[nodiscard]] const GestureSettings& Settings() const noexcept { return settings_; }

    // Only one contact is tracked at a time.  The input layer must call
    // OnContactCancel when a second finger appears or the device disappears.
    void OnContactDown(float x, float y);
    void OnContactMove(float x, float y);
    void OnContactUp();
    void OnContactCancel();
    void Reset();

    [[nodiscard]] bool IsTracking() const noexcept {
        return state_ == State::Pending || state_ == State::Active;
    }
    [[nodiscard]] bool IsActive() const noexcept { return state_ == State::Active; }
    [[nodiscard]] EdgeZone ActiveZone() const noexcept {
        return state_ == State::Active ? zone_ : EdgeZone::None;
    }

private:
    enum class State { Idle, Pending, Active, Blocked };

    struct Point {
        float x = 0.0f;
        float y = 0.0f;
    };

    [[nodiscard]] EdgeZone ResolveZone(float dx, float dy) const;
    void EmitMediaStep(int direction);
    void EmitVolume(float y);
    void EmitBrightness(float y);
    void Finish();

    GestureSettings settings_;
    VolumeFn onVolume_;
    ActionFn onAction_;
    BrightnessFn onBrightness_;

    State state_ = State::Idle;
    EdgeZone zone_ = EdgeZone::None;
    Point start_{};
    Point anchor_{};
    int lastVolumePercent_ = -1;
    int lastBrightnessPercent_ = -1;
};

} // namespace st
