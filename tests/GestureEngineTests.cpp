#include "GestureEngine.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <vector>

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

using namespace st;

struct Rig {
    GestureSettings settings;
    std::vector<int> volume;
    std::vector<EdgeAction> actions;
    std::vector<int> brightness;
    std::uint64_t now = 1000;
    GestureEngine engine;

    explicit Rig(GestureSettings value = {})
        : settings(value),
          engine(settings,
                 [this](int percent) { volume.push_back(percent); },
                 [this](EdgeAction action) { actions.push_back(action); },
                 [this](int percent) { brightness.push_back(percent); },
                 [this] { return now; }) {}

    void Down(float x, float y) { engine.OnContactDown(x, y); }
    void Move(float x, float y) { engine.OnContactMove(x, y); }
    void Up() { engine.OnContactUp(); }
    void Cancel() { engine.OnContactCancel(); }
    // Advances the injected clock, used by the tap and cooldown tests.
    void Advance(std::uint64_t milliseconds) { now += milliseconds; }
};

void CheckEmpty(const Rig& rig) {
    CHECK(rig.volume.empty());
    CHECK(rig.brightness.empty());
    CHECK(rig.actions.empty());
}

void CheckVolume(const Rig& rig, std::initializer_list<int> expected) {
    const std::vector<int> want(expected);
    if (rig.volume != want) {
        std::cerr << "Volume mismatch: got " << rig.volume.size() << ", expected "
                  << want.size() << '\n';
        ++g_failures;
    }
}

void CheckActions(const Rig& rig, std::initializer_list<EdgeAction> expected) {
    const std::vector<EdgeAction> want(expected);
    if (rig.actions != want) {
        std::cerr << "Action mismatch: got " << rig.actions.size() << ", expected "
                  << want.size() << '\n';
        ++g_failures;
    }
}

void CheckBrightness(const Rig& rig, std::initializer_list<int> expected) {
    const std::vector<int> want(expected);
    if (rig.brightness != want) {
        std::cerr << "Brightness mismatch: got " << rig.brightness.size()
                  << ", expected " << want.size() << '\n';
        ++g_failures;
    }
}

void TestLeftEdgeAbsoluteVolume() {
    Rig rig;
    rig.Down(0.05f, 0.40f);
    rig.Move(0.05f, 0.41f); // Below activation threshold.
    CHECK(!rig.engine.IsActive());
    CHECK(rig.volume.empty());

    rig.Move(0.05f, 0.55f); // Crosses the activation threshold at 55% height.
    CHECK(rig.engine.IsActive());
    CHECK(rig.engine.ActiveZone() == EdgeZone::Left);
    CheckVolume(rig, {55});

    rig.Move(0.05f, 0.80f); // Absolute, not incremental.
    CheckVolume(rig, {55, 80});

    rig.Move(0.05f, 0.30f);
    CheckVolume(rig, {55, 80, 30});

    rig.Move(0.05f, 0.30f); // Unchanged percentage: no extra update.
    CheckVolume(rig, {55, 80, 30});

    CHECK(rig.actions.empty());
    CHECK(rig.brightness.empty());
    rig.Up();
    CHECK(!rig.engine.IsTracking());
}

void TestLeftEdgeAbsoluteVolumeDown() {
    Rig rig;
    rig.Down(0.05f, 0.70f);
    rig.Move(0.05f, 0.65f); // Activate downward at 65% height.
    CheckVolume(rig, {65});
    rig.Move(0.05f, 0.20f);
    CheckVolume(rig, {65, 20});
    CHECK(rig.actions.empty());
}

void TestVolumeFollowsBottomToTop() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.00f); // Bottom of the pad = 0%.
    CheckVolume(rig, {0});
    rig.Move(0.05f, 0.00f); // Duplicate value is suppressed.
    CheckVolume(rig, {0});
    rig.Move(0.05f, 1.00f); // Top of the pad = 100%.
    CheckVolume(rig, {0, 100});
    CHECK(rig.actions.empty());
}

void TestRightEdgeAbsoluteBrightness() {
    Rig rig;
    rig.Down(0.95f, 0.50f);
    rig.Move(0.95f, 0.51f); // Below activation.
    CHECK(rig.brightness.empty());

    rig.Move(0.95f, 0.55f); // Activate at 55% height.
    CHECK(rig.engine.ActiveZone() == EdgeZone::Right);
    CheckBrightness(rig, {55});

    rig.Move(0.95f, 0.80f); // Absolute, not incremental.
    CheckBrightness(rig, {55, 80});

    rig.Move(0.95f, 0.30f);
    CheckBrightness(rig, {55, 80, 30});

    rig.Move(0.95f, 0.30f); // Unchanged percentage: no extra update.
    CheckBrightness(rig, {55, 80, 30});

    CHECK(rig.volume.empty());
    CHECK(rig.actions.empty());
}

void TestRightEdgeInvertedBrightness() {
    GestureSettings settings;
    settings.invertBrightness = true;
    Rig rig(settings);

    rig.Down(0.95f, 0.50f);
    rig.Move(0.95f, 0.55f); // 55% -> inverted to 45%.
    CheckBrightness(rig, {45});

    rig.Move(0.95f, 0.20f); // 20% -> inverted to 80%.
    CheckBrightness(rig, {45, 80});
    CHECK(rig.volume.empty());
}

void TestTopEdgeMediaNext() {
    Rig rig;
    rig.Down(0.50f, 0.95f);
    rig.Move(0.53f, 0.95f); // Horizontal activation in the top band.
    CHECK(rig.engine.ActiveZone() == EdgeZone::Top);

    rig.Move(0.75f, 0.95f); // 22% right -> next twice.
    CheckActions(rig, {EdgeAction::MediaNext, EdgeAction::MediaNext});
    CHECK(rig.volume.empty());
    CHECK(rig.brightness.empty());
}

void TestTopEdgeMediaPrevious() {
    Rig rig;
    rig.Down(0.75f, 0.95f);
    rig.Move(0.72f, 0.95f); // Activate.
    rig.Move(0.50f, 0.95f); // 22% left -> previous twice.
    CheckActions(rig, {EdgeAction::MediaPrevious, EdgeAction::MediaPrevious});
    CHECK(rig.volume.empty());
    CHECK(rig.brightness.empty());
}

void TestTopRightCornerAxisDecidesZone() {
    {
        Rig rig;
        rig.Down(0.95f, 0.95f);
        rig.Move(0.95f, 0.90f); // Vertical -> right edge (brightness).
        CHECK(rig.engine.ActiveZone() == EdgeZone::Right);
        CheckBrightness(rig, {90});
        rig.Move(0.95f, 0.80f);
        CheckBrightness(rig, {90, 80});
        CHECK(rig.volume.empty());
        CHECK(rig.actions.empty());
    }
    {
        Rig rig;
        rig.Down(0.95f, 0.95f);
        rig.Move(0.92f, 0.95f); // Horizontal -> top edge (media).
        CHECK(rig.engine.ActiveZone() == EdgeZone::Top);
        rig.Move(0.70f, 0.95f);
        CheckActions(rig, {EdgeAction::MediaPrevious, EdgeAction::MediaPrevious});
        CHECK(rig.volume.empty());
        CHECK(rig.brightness.empty());
    }
}

void TestTopLeftCornerAxisDecidesZone() {
    {
        Rig rig;
        rig.Down(0.05f, 0.95f);
        rig.Move(0.05f, 0.90f); // Vertical -> left edge (volume).
        CHECK(rig.engine.ActiveZone() == EdgeZone::Left);
        CheckVolume(rig, {90});
        rig.Move(0.05f, 0.70f);
        CheckVolume(rig, {90, 70});
        CHECK(rig.actions.empty());
        CHECK(rig.brightness.empty());
    }
    {
        Rig rig;
        rig.Down(0.05f, 0.95f);
        rig.Move(0.08f, 0.95f); // Horizontal -> top edge (media).
        CHECK(rig.engine.ActiveZone() == EdgeZone::Top);
        rig.Move(0.30f, 0.95f);
        CheckActions(rig, {EdgeAction::MediaNext, EdgeAction::MediaNext});
        CHECK(rig.volume.empty());
        CHECK(rig.brightness.empty());
    }
}

void TestCenterIsIgnored() {
    Rig rig;
    rig.Down(0.50f, 0.50f);
    rig.Move(0.53f, 0.50f);
    CHECK(!rig.engine.IsActive());
    rig.Move(0.50f, 0.80f); // Blocked until the finger is lifted.
    CHECK(rig.volume.empty());
    CHECK(rig.actions.empty());
    CHECK(rig.brightness.empty());
    rig.Up();

    // A new gesture outside any zone must not inherit the blocked state.
    rig.Down(0.50f, 0.50f);
    rig.Move(0.50f, 0.80f);
    CHECK(rig.volume.empty());
    CHECK(rig.actions.empty());
    CHECK(rig.brightness.empty());
}

void TestWrongAxisInEdgeBandIsIgnored() {
    {
        Rig rig;
        rig.Down(0.05f, 0.50f); // Left band.
        rig.Move(0.10f, 0.50f); // Horizontal cannot control volume.
        CHECK(!rig.engine.IsActive());
        rig.Move(0.05f, 0.80f);
        CHECK(rig.volume.empty());
        CHECK(rig.actions.empty());
    }
    {
        Rig rig;
        rig.Down(0.50f, 0.95f); // Top band.
        rig.Move(0.50f, 0.90f); // Vertical cannot control media.
        CHECK(!rig.engine.IsActive());
        rig.Move(0.80f, 0.90f);
        CHECK(rig.actions.empty());
        CHECK(rig.volume.empty());
    }
}

void TestZoneStaysLatchedAfterLeavingBand() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.55f); // Activate in the left band.
    CheckVolume(rig, {55});
    rig.Move(0.30f, 0.70f); // Drift into the middle, keep sliding vertically.
    CHECK(rig.engine.ActiveZone() == EdgeZone::Left);
    CheckVolume(rig, {55, 70});
    CHECK(rig.actions.empty());
}

void TestSubStepTravelAccumulates() {
    Rig rig;
    rig.Down(0.50f, 0.95f); // Top edge media test.
    rig.Move(0.53f, 0.95f); // Activate; anchor at 0.53.
    CHECK(rig.engine.ActiveZone() == EdgeZone::Top);
    rig.Move(0.60f, 0.95f); // Only 7% from the anchor -> no step yet.
    CHECK(rig.actions.empty());
    rig.Move(0.72f, 0.95f); // 19% from the anchor -> one step.
    CheckActions(rig, {EdgeAction::MediaNext});
    CHECK(rig.volume.empty());
    CHECK(rig.brightness.empty());
}

void TestDisablingZones() {
    {
        GestureSettings settings;
        settings.leftEnabled = false;
        Rig rig(settings);

        rig.Down(0.05f, 0.50f);
        rig.Move(0.05f, 0.55f);
        CHECK(!rig.engine.IsActive());
        rig.Move(0.05f, 0.80f);
        CHECK(rig.volume.empty());
        CHECK(rig.actions.empty());
    }
    {
        GestureSettings settings;
        settings.rightEnabled = false;
        Rig rig(settings);

        rig.Down(0.95f, 0.50f);
        rig.Move(0.95f, 0.55f);
        CHECK(!rig.engine.IsActive());
        rig.Move(0.95f, 0.80f);
        CHECK(rig.brightness.empty());
        CHECK(rig.volume.empty());
    }
}

void TestInversion() {
    {
        GestureSettings settings;
        settings.invertVolume = true;
        Rig rig(settings);
        rig.Down(0.05f, 0.50f);
        rig.Move(0.05f, 0.55f); // 55% -> inverted to 45%.
        rig.Move(0.05f, 0.80f); // 80% -> inverted to 20%.
        CheckVolume(rig, {45, 20});
        CHECK(rig.actions.empty());
    }
    {
        GestureSettings settings;
        settings.invertMedia = true;
        Rig rig(settings);
        rig.Down(0.50f, 0.95f);
        rig.Move(0.53f, 0.95f);
        rig.Move(0.75f, 0.95f);
        CheckActions(rig, {EdgeAction::MediaPrevious, EdgeAction::MediaPrevious});
        CHECK(rig.volume.empty());
    }
}

void TestNewGestureAfterLift() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.55f);
    CheckVolume(rig, {55});
    rig.Up();
    CHECK(!rig.engine.IsTracking());

    rig.Down(0.95f, 0.50f);
    rig.Move(0.95f, 0.55f);
    CHECK(rig.engine.ActiveZone() == EdgeZone::Right);
    CheckBrightness(rig, {55});
    CheckVolume(rig, {55});
}

void TestBrightnessFloorClamp() {
    GestureSettings settings;
    settings.minBrightnessPercent = 20;
    Rig rig(settings);

    rig.Down(0.95f, 0.50f);
    rig.Move(0.95f, 0.55f);
    CheckBrightness(rig, {55});

    rig.Move(0.95f, 0.00f); // Bottom of the pad would be 0%; the floor wins.
    CheckBrightness(rig, {55, 20});

    rig.Move(0.95f, 0.10f); // 10% still below the floor: no new value.
    CheckBrightness(rig, {55, 20});

    rig.Move(0.95f, 1.00f);
    CheckBrightness(rig, {55, 20, 100});
}

void TestVolumeRangeClamp() {
    GestureSettings settings;
    settings.minVolumePercent = 15;
    settings.maxVolumePercent = 80;
    Rig rig(settings);

    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.05f);
    rig.Move(0.05f, 0.95f);
    CheckVolume(rig, {15, 80});
}

void TestTapOnLeftEdgeMutes() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Advance(80);
    rig.Up();
    CheckActions(rig, {EdgeAction::ToggleMute});
    CHECK(rig.volume.empty());
    CHECK(rig.brightness.empty());
}

void TestTapOnRightEdgeTurnsDisplayOff() {
    Rig rig;
    rig.Down(0.95f, 0.60f);
    rig.Advance(120);
    rig.Up();
    CheckActions(rig, {EdgeAction::MonitorOff});
    CHECK(rig.volume.empty());
}

void TestTapOnTopEdgeTogglesPlayback() {
    Rig rig;
    rig.Down(0.50f, 0.95f);
    rig.Advance(100);
    rig.Up();
    CheckActions(rig, {EdgeAction::MediaPlayPause});
}

void TestTapInCornerPrefersVerticalEdge() {
    Rig rig;
    rig.Down(0.05f, 0.95f);
    rig.Advance(100);
    rig.Up();
    CheckActions(rig, {EdgeAction::ToggleMute});
}

void TestTapRejectedWhenHeldTooLong() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Advance(900); // Longer than the default 350 ms tap window.
    rig.Up();
    CheckEmpty(rig);
}

void TestTapRejectedAfterMovement() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.55f); // Crosses activation: this is a slide, not a tap.
    CheckVolume(rig, {55});
    rig.Up();
    CheckActions(rig, {});
}

void TestTapRejectedInTheMiddle() {
    Rig rig;
    rig.Down(0.50f, 0.50f);
    rig.Advance(100);
    rig.Up();
    CheckEmpty(rig);
}

void TestTapsCanBeDisabled() {
    GestureSettings settings;
    settings.tapsEnabled = false;
    Rig rig(settings);
    rig.Down(0.05f, 0.50f);
    rig.Advance(100);
    rig.Up();
    CheckEmpty(rig);
}

void TestCooldownAfterCancel() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.55f);
    CheckVolume(rig, {55});

    rig.Cancel(); // Second finger: the pad must settle before a new gesture.
    CHECK(!rig.engine.IsTracking());
    CHECK(rig.engine.IsCoolingDown());

    rig.Down(0.05f, 0.50f);
    CHECK(!rig.engine.IsTracking());
    rig.Move(0.05f, 0.70f);
    CheckVolume(rig, {55});

    rig.Advance(200);
    CHECK(!rig.engine.IsCoolingDown());
    rig.Down(0.05f, 0.50f);
    CHECK(rig.engine.IsTracking());
    rig.Move(0.05f, 0.70f);
    CheckVolume(rig, {55, 70});
}

void TestNormalLiftDoesNotStartCooldown() {
    Rig rig;
    rig.Down(0.05f, 0.50f);
    rig.Move(0.05f, 0.55f);
    rig.Up();
    CHECK(!rig.engine.IsCoolingDown());

    rig.Down(0.95f, 0.50f); // An immediate new gesture is accepted.
    CHECK(rig.engine.IsTracking());
}

} // namespace

#define RUN_TEST(test)          \
    do {                        \
        std::cout << #test << '\n'; \
        test();                 \
    } while (false)

int main() {
    RUN_TEST(TestLeftEdgeAbsoluteVolume);
    RUN_TEST(TestLeftEdgeAbsoluteVolumeDown);
    RUN_TEST(TestVolumeFollowsBottomToTop);
    RUN_TEST(TestRightEdgeAbsoluteBrightness);
    RUN_TEST(TestRightEdgeInvertedBrightness);
    RUN_TEST(TestTopEdgeMediaNext);
    RUN_TEST(TestTopEdgeMediaPrevious);
    RUN_TEST(TestTopRightCornerAxisDecidesZone);
    RUN_TEST(TestTopLeftCornerAxisDecidesZone);
    RUN_TEST(TestCenterIsIgnored);
    RUN_TEST(TestWrongAxisInEdgeBandIsIgnored);
    RUN_TEST(TestZoneStaysLatchedAfterLeavingBand);
    RUN_TEST(TestSubStepTravelAccumulates);
    RUN_TEST(TestDisablingZones);
    RUN_TEST(TestInversion);
    RUN_TEST(TestNewGestureAfterLift);
    RUN_TEST(TestBrightnessFloorClamp);
    RUN_TEST(TestVolumeRangeClamp);
    RUN_TEST(TestTapOnLeftEdgeMutes);
    RUN_TEST(TestTapOnRightEdgeTurnsDisplayOff);
    RUN_TEST(TestTapOnTopEdgeTogglesPlayback);
    RUN_TEST(TestTapInCornerPrefersVerticalEdge);
    RUN_TEST(TestTapRejectedWhenHeldTooLong);
    RUN_TEST(TestTapRejectedAfterMovement);
    RUN_TEST(TestTapRejectedInTheMiddle);
    RUN_TEST(TestTapsCanBeDisabled);
    RUN_TEST(TestCooldownAfterCancel);
    RUN_TEST(TestNormalLiftDoesNotStartCooldown);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All gesture engine tests passed.\n";
    return 0;
}
