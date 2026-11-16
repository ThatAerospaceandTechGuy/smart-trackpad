#include "ContactTracker.h"

#include <iostream>
#include <string>
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

using st::ContactSample;
using st::ContactTracker;
using st::hid::Contact;

class Rig {
public:
    Rig()
        : tracker_(ContactTracker::Callbacks{
              [this](float x, float y) { events.push_back("down"); lastX = x; lastY = y; },
              [this](float x, float y) { events.push_back("move"); lastX = x; lastY = y; },
              [this] { events.push_back("up"); },
              [this] { events.push_back("cancel"); }}) {}

    void Down(float x = 0.5f, float y = 0.5f) {
        ContactSample sample;
        sample.contact = Contact::Down;
        sample.hasPosition = true;
        sample.x = x;
        sample.y = y;
        tracker_.Feed(key, sample);
    }

    void Move(float x, float y) {
        ContactSample sample;
        sample.contact = Contact::Down;
        sample.hasPosition = true;
        sample.x = x;
        sample.y = y;
        tracker_.Feed(key, sample);
    }

    void Up() {
        ContactSample sample;
        sample.contact = Contact::Up;
        tracker_.Feed(key, sample);
    }

    void Multiple() {
        ContactSample sample;
        sample.contact = Contact::Multiple;
        tracker_.Feed(key, sample);
    }

    void NoPosition(bool reportsTipSwitch) {
        ContactSample sample;
        sample.contact = Contact::Unknown;
        sample.hasPosition = false;
        sample.reportsTipSwitch = reportsTipSwitch;
        tracker_.Feed(key, sample);
    }

    void FeedOther(const void* other) {
        ContactSample sample;
        sample.contact = Contact::Down;
        sample.hasPosition = true;
        sample.x = 0.9f;
        sample.y = 0.1f;
        tracker_.Feed(other, sample);
    }

    int keyStorage = 0;
    const void* key = &keyStorage;
    ContactTracker tracker_;
    std::vector<std::string> events;
    float lastX = 0.0f;
    float lastY = 0.0f;
};

void TestDownMoveUp() {
    Rig rig;
    CHECK(!rig.tracker_.IsTracking());
    rig.Down(0.2f, 0.8f);
    CHECK(rig.tracker_.IsTracking());
    CHECK(rig.lastX == 0.2f);
    CHECK(rig.lastY == 0.8f);

    rig.Move(0.3f, 0.7f);
    rig.Up();
    CHECK(!rig.tracker_.IsTracking());
    CHECK(rig.events.size() == 3);
    CHECK(rig.events[0] == "down");
    CHECK(rig.events[1] == "move");
    CHECK(rig.events[2] == "up");
}

void TestSecondFingerCancelsUntilRelease() {
    Rig rig;
    rig.Down();
    rig.Multiple();
    CHECK(!rig.tracker_.IsTracking());
    CHECK(rig.events.size() == 2);
    CHECK(rig.events[1] == "cancel");

    // The pad is still hot: a new down must be ignored until every finger is
    // lifted.
    rig.Down();
    CHECK(!rig.tracker_.IsTracking());

    rig.Up();
    rig.Down();
    CHECK(rig.tracker_.IsTracking());
}

void TestReportsWithoutPositionAreIgnored() {
    Rig rig;
    rig.Down();
    rig.NoPosition(true); // Tip switch device: wait for coordinates.
    CHECK(rig.tracker_.IsTracking());
    CHECK(rig.events.size() == 1);
}

void TestPositionlessDigitizerSignalsRelease() {
    Rig rig;
    rig.Down();
    rig.NoPosition(false); // No tip switch: an empty report is a lift.
    CHECK(!rig.tracker_.IsTracking());
    CHECK(rig.events.back() == "up");
}

void TestSecondDeviceIsIgnoredWhileTracking() {
    Rig rig;
    int other = 0;
    rig.Down(0.1f, 0.1f);
    rig.FeedOther(&other);
    CHECK(rig.tracker_.IsTracking());
    CHECK(rig.events.size() == 1);
    CHECK(rig.lastX == 0.1f);
}

void TestUnknownContactWithPositionStartsTracking() {
    Rig rig;
    ContactSample sample;
    sample.contact = Contact::Unknown;
    sample.hasPosition = true;
    sample.x = 0.42f;
    sample.y = 0.58f;
    rig.tracker_.Feed(rig.key, sample);
    CHECK(rig.tracker_.IsTracking());
    CHECK(rig.events.size() == 1);
    CHECK(rig.events[0] == "down");
}

void TestDeviceRemovalCancels() {
    Rig rig;
    rig.Down();
    rig.tracker_.DeviceRemoved(rig.key);
    CHECK(!rig.tracker_.IsTracking());
    CHECK(rig.events.back() == "cancel");

    // A removed device leaves no blocked state behind.
    rig.Down();
    CHECK(rig.tracker_.IsTracking());
}

void TestResetCancels() {
    Rig rig;
    rig.Down();
    rig.tracker_.Reset();
    CHECK(!rig.tracker_.IsTracking());
    CHECK(rig.events.back() == "cancel");
}

} // namespace

#define RUN_TEST(test)              \
    do {                            \
        std::cout << #test << '\n'; \
        test();                     \
    } while (false)

int main() {
    RUN_TEST(TestDownMoveUp);
    RUN_TEST(TestSecondFingerCancelsUntilRelease);
    RUN_TEST(TestReportsWithoutPositionAreIgnored);
    RUN_TEST(TestPositionlessDigitizerSignalsRelease);
    RUN_TEST(TestSecondDeviceIsIgnoredWhileTracking);
    RUN_TEST(TestUnknownContactWithPositionStartsTracking);
    RUN_TEST(TestDeviceRemovalCancels);
    RUN_TEST(TestResetCancels);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All contact tracker tests passed.\n";
    return 0;
}
