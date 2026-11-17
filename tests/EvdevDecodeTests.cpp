#include "EvdevDecode.h"

#include <cmath>
#include <iostream>

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

using namespace st::evdev;

bool NearlyEqual(float left, float right) {
    return std::fabs(left - right) < 1e-4f;
}

// A 3080x2064 touchpad, the same range the Windows dev machine reports.
Decoder MakeDecoder() {
    return Decoder(AxisRange{0, 3080}, AxisRange{0, 2064});
}

void TestSingleFingerMapsToNormalizedPosition() {
    Decoder decoder = MakeDecoder();

    // One finger at the middle of the pad.
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 1);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 1540);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 1032);

    const Report report = decoder.Commit(true);
    CHECK(report.contact == st::hid::Contact::Down);
    CHECK(report.hasPosition);
    CHECK(NearlyEqual(report.x, 0.5f));
    // evdev Y grows downward, the engine wants bottom-up: the middle stays 0.5.
    CHECK(NearlyEqual(report.y, 0.5f));
}

void TestBottomEdgeIsLowY() {
    Decoder decoder = MakeDecoder();

    // Bottom-left corner of the pad in evdev coordinates is the left edge at
    // 0% for the engine.
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 3);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 0);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 2064);

    const Report report = decoder.Commit(true);
    CHECK(NearlyEqual(report.x, 0.0f));
    CHECK(NearlyEqual(report.y, 0.0f));
}

void TestTopRightCornerIsOneOne() {
    Decoder decoder = MakeDecoder();

    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 4);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 3080);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 0);

    const Report report = decoder.Commit(true);
    CHECK(NearlyEqual(report.x, 1.0f));
    CHECK(NearlyEqual(report.y, 1.0f));
}

void TestTwoFingersReportMultiple() {
    Decoder decoder = MakeDecoder();

    decoder.Input(kEventTypeAbs, kAbsMtSlot, 0);
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 1);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 500);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 500);

    decoder.Input(kEventTypeAbs, kAbsMtSlot, 1);
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 2);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 2000);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 1200);

    const Report report = decoder.Commit(true);
    CHECK(report.contact == st::hid::Contact::Multiple);
    CHECK(!report.hasPosition);
}

void TestLiftedFingerReleasesSlot() {
    Decoder decoder = MakeDecoder();

    decoder.Input(kEventTypeAbs, kAbsMtSlot, 0);
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 1);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 500);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 500);
    CHECK(decoder.Commit(true).contact == st::hid::Contact::Down);

    // Tracking id -1 means the finger left this slot.
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, -1);
    CHECK(decoder.Commit(false).contact == st::hid::Contact::Up);
}

void TestTipWithoutSlotsUsesSingleTouchAxes() {
    Decoder decoder = MakeDecoder();

    // Some touchpads only report ABS_X/ABS_Y plus BTN_TOUCH.
    decoder.Input(kEventTypeAbs, kAbsX, 308);
    decoder.Input(kEventTypeAbs, kAbsY, 206);

    const Report report = decoder.Commit(true);
    CHECK(report.contact == st::hid::Contact::Down);
    // 10% across, and 206 of 2064 from the top of the pad.
    CHECK(NearlyEqual(report.x, 308.0f / 3080.0f));
    CHECK(NearlyEqual(report.y, 1.0f - 206.0f / 2064.0f));
}

void TestNonPositionalEventsAreIgnored() {
    Decoder decoder = MakeDecoder();

    // Pressure, touch-major, misc and key events must not move the contact.
    decoder.Input(0x03, 0x3a, 42); // ABS_MT_PRESSURE
    decoder.Input(0x01, 0x14a, 1); // EV_KEY BTN_TOUCH
    decoder.Input(0x04, 0x04, 0);  // EV_MSC MSC_SCAN
    decoder.Input(0x00, 0x00, 0);  // EV_SYN

    const Report report = decoder.Commit(false);
    CHECK(report.contact == st::hid::Contact::Up);
    CHECK(!report.hasPosition);
}

void TestResetClearsSlots() {
    Decoder decoder = MakeDecoder();

    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 1);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 1000);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 1000);
    CHECK(decoder.Commit(true).contact == st::hid::Contact::Down);

    decoder.Reset();
    CHECK(decoder.Commit(false).contact == st::hid::Contact::Up);
}

void TestOutOfRangeSlotIsIgnored() {
    Decoder decoder = MakeDecoder();

    decoder.Input(kEventTypeAbs, kAbsMtSlot, 99);
    decoder.Input(kEventTypeAbs, kAbsMtTrackingId, 1);
    decoder.Input(kEventTypeAbs, kAbsMtPositionX, 100);
    decoder.Input(kEventTypeAbs, kAbsMtPositionY, 100);

    // The slot index was rejected, so the events landed in slot 0.
    const Report report = decoder.Commit(true);
    CHECK(report.contact == st::hid::Contact::Down);
    CHECK(NearlyEqual(report.x, 100.0f / 3080.0f));
}

} // namespace

#define RUN_TEST(test)              \
    do {                            \
        std::cout << #test << '\n'; \
        test();                     \
    } while (false)

int main() {
    RUN_TEST(TestSingleFingerMapsToNormalizedPosition);
    RUN_TEST(TestBottomEdgeIsLowY);
    RUN_TEST(TestTopRightCornerIsOneOne);
    RUN_TEST(TestTwoFingersReportMultiple);
    RUN_TEST(TestLiftedFingerReleasesSlot);
    RUN_TEST(TestTipWithoutSlotsUsesSingleTouchAxes);
    RUN_TEST(TestNonPositionalEventsAreIgnored);
    RUN_TEST(TestResetClearsSlots);
    RUN_TEST(TestOutOfRangeSlotIsIgnored);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All evdev decode tests passed.\n";
    return 0;
}
