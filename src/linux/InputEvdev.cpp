#include "InputEvdev.h"

#include "EvdevDecode.h"
#include "HidDecode.h"
#include "Log.h"
#include "Text.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <vector>

// The duplicated constants in EvdevDecode.h must match the kernel headers.
static_assert(st::evdev::kAbsX == ABS_X);
static_assert(st::evdev::kAbsY == ABS_Y);
static_assert(st::evdev::kAbsMtSlot == ABS_MT_SLOT);
static_assert(st::evdev::kAbsMtPositionX == ABS_MT_POSITION_X);
static_assert(st::evdev::kAbsMtPositionY == ABS_MT_POSITION_Y);
static_assert(st::evdev::kAbsMtTrackingId == ABS_MT_TRACKING_ID);
static_assert(st::evdev::kEventTypeAbs == EV_ABS);

namespace st {
namespace {

// An evdev axis as reported by EVIOCGABS.
struct AxisRange {
    int minimum = 0;
    int maximum = 0;
    bool valid = false;
};

AxisRange ReadAxis(int fd, int axis) {
    input_absinfo info{};
    if (ioctl(fd, EVIOCGABS(axis), &info) < 0) {
        return {};
    }
    return AxisRange{info.minimum, info.maximum, true};
}

bool TestBit(const unsigned long* bits, int bit) {
    return (bits[bit / (8 * sizeof(unsigned long))] &
            (1UL << (bit % (8 * sizeof(unsigned long))))) != 0;
}

bool HasAxis(int fd, int axis) {
    unsigned long bits[(ABS_MAX / (8 * sizeof(unsigned long))) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) {
        return false;
    }
    return TestBit(bits, axis);
}

bool HasProperty(int fd, int property) {
    unsigned long bits[(INPUT_PROP_MAX / (8 * sizeof(unsigned long))) + 1] = {};
    if (ioctl(fd, EVIOCGPROP(sizeof(bits)), bits) < 0) {
        return false;
    }
    return TestBit(bits, property);
}

std::string DeviceName(int fd) {
    char name[256]{};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        return {};
    }
    return name;
}

} // namespace

struct EvdevInput::Device {
    int fd = -1;
    std::string path;
    std::string name;
    bool isDirect = false; // INPUT_PROP_DIRECT: a touchscreen, not a touchpad
    bool hasMultitouch = false;
    bool tipDown = false;
    evdev::Decoder decoder{evdev::AxisRange{}, evdev::AxisRange{}};
};

EvdevInput::EvdevInput(Callbacks callbacks)
    : tracker_(ContactTracker::Callbacks{
          callbacks.onContactDown, callbacks.onContactMove, callbacks.onContactUp,
          callbacks.onCancel}) {}

EvdevInput::~EvdevInput() {
    Stop();
}

bool EvdevInput::OpenDevice(const std::string& path, DigitizerDevices devices) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    const bool hasMulti = HasAxis(fd, ABS_MT_POSITION_X) && HasAxis(fd, ABS_MT_POSITION_Y);
    const bool hasSingle = HasAxis(fd, ABS_X) && HasAxis(fd, ABS_Y);
    if (!hasMulti && !hasSingle) {
        close(fd);
        return false;
    }

    const bool isDirect = HasProperty(fd, INPUT_PROP_DIRECT);

    // A touchpad is an indirect pointer device; a touchscreen is direct.
    const bool wantsTouchpad = devices != DigitizerDevices::Touchscreen;
    const bool wantsTouchscreen = devices != DigitizerDevices::Touchpad;
    if (isDirect ? !wantsTouchscreen : !wantsTouchpad) {
        close(fd);
        return false;
    }

    auto device = std::make_unique<Device>();
    device->fd = fd;
    device->path = path;
    device->name = DeviceName(fd);
    device->isDirect = isDirect;
    device->hasMultitouch = hasMulti;
    const AxisRange x = ReadAxis(fd, hasMulti ? ABS_MT_POSITION_X : ABS_X);
    const AxisRange y = ReadAxis(fd, hasMulti ? ABS_MT_POSITION_Y : ABS_Y);

    if (!x.valid || !y.valid || x.maximum <= x.minimum || y.maximum <= y.minimum) {
        LogFormat(LogLevel::Debug,
                  L"Input: \"%hs\" has no usable absolute range; ignored.",
                  path.c_str());
        close(fd);
        return false;
    }

    device->decoder =
        evdev::Decoder(evdev::AxisRange{x.minimum, x.maximum},
                       evdev::AxisRange{y.minimum, y.maximum});

    LogFormat(LogLevel::Info, L"Input: \"%hs\" %ls (X %d..%d, Y %d..%d, %ls).",
              device->name.c_str(), isDirect ? L"touchscreen" : L"touchpad",
              x.minimum, x.maximum, y.minimum, y.maximum,
              hasMulti ? L"multitouch" : L"single touch");

    devices_.push_back(std::move(device));
    return true;
}

bool EvdevInput::Start(DigitizerDevices devices) {
    Stop();

    DIR* directory = opendir("/dev/input");
    if (directory == nullptr) {
        LogMessage(LogLevel::Warning,
                   L"Input: /dev/input is not readable; edge gestures need access to "
                   L"the event devices (add your user to the \"input\" group).");
        return false;
    }

    std::vector<std::string> paths;
    while (dirent* entry = readdir(directory)) {
        const std::string name(entry->d_name);
        if (name.rfind("event", 0) == 0) {
            paths.push_back("/dev/input/" + name);
        }
    }
    closedir(directory);

    std::sort(paths.begin(), paths.end());
    for (const std::string& path : paths) {
        OpenDevice(path, devices);
    }

    if (devices_.empty()) {
        LogMessage(LogLevel::Warning,
                   L"Input: no absolute digitizer found. Edge gestures need a "
                   L"touchpad that reports absolute coordinates through evdev "
                   L"(run with --zone-test to see what is detected).");
        return false;
    }
    return true;
}

void EvdevInput::Stop() {
    for (const std::unique_ptr<Device>& device : devices_) {
        if (device->fd >= 0) {
            close(device->fd);
        }
    }
    devices_.clear();
    tracker_.Reset();
}

void EvdevInput::Refresh(DigitizerDevices devices) {
    Start(devices);
}

std::vector<int> EvdevInput::FileDescriptors() const {
    std::vector<int> descriptors;
    descriptors.reserve(devices_.size());
    for (const std::unique_ptr<Device>& device : devices_) {
        if (device->fd >= 0) {
            descriptors.push_back(device->fd);
        }
    }
    return descriptors;
}

bool EvdevInput::HasDevices() const {
    return !devices_.empty();
}

void EvdevInput::HandleReadable() {
    for (const std::unique_ptr<Device>& instance : devices_) {
        Device& device = *instance;
        if (device.fd < 0) {
            continue;
        }

        input_event events[64];
        const ssize_t bytes = read(device.fd, events, sizeof(events));
        if (bytes <= 0) {
            if (bytes < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            LogFormat(LogLevel::Info, L"Input: \"%hs\" disappeared.",
                      device.name.c_str());
            tracker_.DeviceRemoved(&device);
            close(device.fd);
            device.fd = -1;
            continue;
        }

        const std::size_t count = static_cast<std::size_t>(bytes) / sizeof(input_event);
        for (std::size_t index = 0; index < count; ++index) {
            const input_event& event = events[index];

            if (event.type == EV_KEY && (event.code == BTN_TOUCH ||
                                          event.code == BTN_TOOL_FINGER ||
                                          event.code == BTN_MOUSE)) {
                device.tipDown = event.value != 0;
                continue;
            }

            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                const evdev::Report report = device.decoder.Commit(device.tipDown);
                ContactSample sample;
                sample.contact = report.contact;
                sample.hasPosition = report.hasPosition;
                sample.x = report.x;
                sample.y = report.y;
                // Every evdev touchpad reports BTN_TOUCH, so a report without a
                // position is not a release.
                sample.reportsTipSwitch = true;
                tracker_.Feed(&device, sample);
                continue;
            }

            device.decoder.Input(event.type, event.code, event.value);
        }
    }
}

std::wstring EvdevInput::DescribeDevices() const {
    if (devices_.empty()) {
        return L"No absolute digitizer device is open.";
    }

    std::wstring summary;
    for (const std::unique_ptr<Device>& device : devices_) {
        if (!summary.empty()) {
            summary += L", ";
        }
        summary += text::ToWide(device->name.empty() ? device->path : device->name);
    }

    wchar_t prefix[64]{};
    swprintf(prefix, std::size(prefix), L"%d device(s): ",
             static_cast<int>(devices_.size()));
    return std::wstring(prefix) + summary;
}

} // namespace st
