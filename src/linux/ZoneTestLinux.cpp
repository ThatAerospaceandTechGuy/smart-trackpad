#include "ZoneTestLinux.h"

#include "GestureEngine.h"
#include "InputEvdev.h"
#include "Log.h"
#include "Text.h"

#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace st {
namespace {

volatile sig_atomic_t g_stop = 0;

void OnSignal(int) {
    g_stop = 1;
}

const char* ZoneName(EdgeZone zone) {
    switch (zone) {
    case EdgeZone::Left:
        return "left (volume)";
    case EdgeZone::Right:
        return "right (brightness)";
    case EdgeZone::Top:
        return "top (media)";
    case EdgeZone::None:
        break;
    }
    return "-";
}

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

int RunZoneTest(const AppConfig& config) {
    std::printf("Smart Trackpad - zone test\n");
    std::printf("Touch the edges of the touchpad. Ctrl+C ends the test.\n\n");

    EvdevInput input(EvdevInput::Callbacks{
        [](float x, float y) { std::printf("down  x=%.3f y=%.3f\n", x, y); },
        [](float x, float y) { std::printf("move  x=%.3f y=%.3f\n", x, y); },
        [] { std::printf("up\n"); },
        [] { std::printf("cancel\n"); }});

    if (!input.Start(config.devices)) {
        std::printf("\nNo absolute digitizer was found.\n"
                    "Check `ls -l /dev/input/` and your group membership "
                    "(input/plugdev).\n");
        return 1;
    }

    std::printf("%ls\n\n", input.DescribeDevices().c_str());
    std::fflush(stdout);

    // A dry-run engine: it reports the resolved zone and value but its callbacks
    // do nothing, so testing never changes the volume or the display.
    GestureEngine engine(
        config.gestures, [](int) {}, [](EdgeAction) {}, [](int) {}, [] { return NowMs(); });

    struct sigaction action {};
    action.sa_handler = &OnSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    while (g_stop == 0) {
        std::vector<int> descriptors = input.FileDescriptors();
        std::vector<pollfd> pollSet;
        pollSet.reserve(descriptors.size());
        for (const int descriptor : descriptors) {
            pollSet.push_back(pollfd{descriptor, POLLIN, 0});
        }
        if (pollSet.empty()) {
            break;
        }

        if (poll(&pollSet[0], pollSet.size(), 200) <= 0) {
            continue;
        }

        input.HandleReadable();
        if (const EdgeZone zone = engine.ActiveZone(); zone != EdgeZone::None) {
            std::printf("      zone %s\n", ZoneName(zone));
            std::fflush(stdout);
        }
    }

    input.Stop();
    return 0;
}

int RunSettingsEditor(const AppConfig& config) {
    const char* editor = std::getenv("VISUAL");
    if (editor == nullptr || editor[0] == '\0') {
        editor = std::getenv("EDITOR");
    }
    if (editor == nullptr || editor[0] == '\0') {
        editor = "nano";
    }

    const std::string path = config.configPath.string();
    std::printf("Opening %s with %s.\n", path.c_str(), editor);
    std::fflush(stdout);

    const pid_t child = fork();
    if (child == 0) {
        execlp(editor, editor, path.c_str(), static_cast<char*>(nullptr));
        std::fprintf(stderr, "Could not run %s.\n", editor);
        _exit(127);
    }
    if (child < 0) {
        std::fprintf(stderr, "Could not start the editor.\n");
        return 1;
    }

    int status = 0;
    (void)waitpid(child, &status, 0);
    std::printf("Saved; a running instance reloads the file automatically.\n");
    return 0;
}

} // namespace st
