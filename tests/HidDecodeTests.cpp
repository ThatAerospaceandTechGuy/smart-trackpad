#include "HidDecode.h"

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

using st::hid::CombineContactSignals;
using st::hid::Contact;
using st::hid::ContactSignals;
using st::hid::NormalizeValue;
using st::hid::SignExtend;
using st::hid::TipQuery;

bool NearlyEqual(float left, float right) {
    return std::fabs(left - right) < 1e-6f;
}

void TestSignExtension() {
    // Unsigned ranges keep their value.
    CHECK(SignExtend(0u, 12) == 0);
    CHECK(SignExtend(1234u, 12) == 1234);

    // 12-bit two's complement: 0x800 is -2048, 0xFFF is -1.
    CHECK(SignExtend(0x800u, 12) == -2048);
    CHECK(SignExtend(0xFFFu, 12) == -1);
    CHECK(SignExtend(0x7FFu, 12) == 2047);

    // 16-bit two's complement.
    CHECK(SignExtend(0x8000u, 16) == -32768);
    CHECK(SignExtend(0xFFFFu, 16) == -1);

    // A bit size of 0 or 32+ passes the value through as a 32-bit field.
    CHECK(SignExtend(0xFFFFFFFFu, 0) == -1);
    CHECK(SignExtend(0xFFFFFFFFu, 32) == -1);
}

void TestNormalizeUnsignedRange() {
    CHECK(NearlyEqual(NormalizeValue(0u, 0, 3080, 12), 0.0f));
    CHECK(NearlyEqual(NormalizeValue(3080u, 0, 3080, 12), 1.0f));
    CHECK(NearlyEqual(NormalizeValue(1540u, 0, 3080, 12), 0.5f));

    // Values outside the declared range are clamped, not extrapolated.
    CHECK(NearlyEqual(NormalizeValue(5000u, 0, 3080, 12), 1.0f));
}

void TestNormalizeSignedRange() {
    // -128..127 with an 8-bit signed field.
    CHECK(NearlyEqual(NormalizeValue(0x80u, -128, 127, 8), 0.0f));
    CHECK(NearlyEqual(NormalizeValue(0x7Fu, -128, 127, 8), 1.0f));
    CHECK(NearlyEqual(NormalizeValue(0x00u, -128, 127, 8),
                     128.0f / 255.0f));

    // -32768..32767 with a 16-bit signed field.
    CHECK(NearlyEqual(NormalizeValue(0x8000u, -32768, 32767, 16), 0.0f));
    CHECK(NearlyEqual(NormalizeValue(0x7FFFu, -32768, 32767, 16), 1.0f));
}

void TestNormalizeDegenerateRange() {
    CHECK(NearlyEqual(NormalizeValue(10u, 5, 5, 8), 0.5f));
    CHECK(NearlyEqual(NormalizeValue(10u, 5, 4, 8), 0.5f));
}

void TestContactSingle() {
    ContactSignals signals;
    signals.tipQuery = TipQuery::Ok;
    signals.tipCount = 1;
    CHECK(CombineContactSignals(signals) == Contact::Down);

    signals.hasContactCount = true;
    signals.contactCount = 1;
    CHECK(CombineContactSignals(signals) == Contact::Down);
}

void TestContactUp() {
    ContactSignals signals;
    signals.tipQuery = TipQuery::Ok;
    signals.tipCount = 0;
    CHECK(CombineContactSignals(signals) == Contact::Up);

    // Contact Count wins over a contradictory tip-switch result.
    signals.tipQuery = TipQuery::Ok;
    signals.tipCount = 1;
    signals.hasContactCount = true;
    signals.contactCount = 0;
    CHECK(CombineContactSignals(signals) == Contact::Up);
}

void TestContactMultiple() {
    ContactSignals signals;
    signals.tipQuery = TipQuery::Ok;
    signals.tipCount = 3;
    CHECK(CombineContactSignals(signals) == Contact::Multiple);

    ContactSignals overflow;
    overflow.tipQuery = TipQuery::BufferTooSmall;
    CHECK(CombineContactSignals(overflow) == Contact::Multiple);

    ContactSignals counted;
    counted.tipQuery = TipQuery::Ok;
    counted.tipCount = 1;
    counted.hasContactCount = true;
    counted.contactCount = 2;
    CHECK(CombineContactSignals(counted) == Contact::Multiple);
}

void TestContactUnknownAndFailure() {
    ContactSignals signals;
    CHECK(CombineContactSignals(signals) == Contact::Unknown);

    signals.tipQuery = TipQuery::Failed;
    signals.tipCount = 0;
    CHECK(CombineContactSignals(signals) == Contact::Unknown);

    // A contact-count-only device can still report a single contact.
    signals.hasContactCount = true;
    signals.contactCount = 1;
    CHECK(CombineContactSignals(signals) == Contact::Down);

    // ... and a lift, even when the tip query failed.
    signals.contactCount = 0;
    CHECK(CombineContactSignals(signals) == Contact::Up);
}

} // namespace

#define RUN_TEST(test)              \
    do {                            \
        std::cout << #test << '\n'; \
        test();                     \
    } while (false)

int main() {
    RUN_TEST(TestSignExtension);
    RUN_TEST(TestNormalizeUnsignedRange);
    RUN_TEST(TestNormalizeSignedRange);
    RUN_TEST(TestNormalizeDegenerateRange);
    RUN_TEST(TestContactSingle);
    RUN_TEST(TestContactUp);
    RUN_TEST(TestContactMultiple);
    RUN_TEST(TestContactUnknownAndFailure);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All HID decode tests passed.\n";
    return 0;
}
