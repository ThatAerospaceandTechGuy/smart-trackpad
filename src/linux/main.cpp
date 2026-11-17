#include "AppLinux.h"

#include "Config.h"
#include "CrashHandlerPosix.h"
#include "Log.h"
#include "Platform.h"
#include "Text.h"
#include "ZoneTestLinux.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <filesystem>
#include <string>
#include <system_error>

// A narrow form of the version for printf/usage output.  CMake defines the wide
// `SMARTTRACKPAD_VERSION` used by the log.
#ifndef SMARTTRACKPAD_VERSION_TEXT
#define SMARTTRACKPAD_VERSION_TEXT "0.0.0"
#endif

namespace {

// An abstract socket (sun_path starting with a NUL) leaves no file behind and
// cannot collide with another user's instance.
constexpr std::string_view kSocketName{"smarttrackpad-command"};

socklen_t BuildSocketAddress(sockaddr_un* address) {
    std::memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    address->sun_path[0] = '\0';
    std::memcpy(address->sun_path + 1, kSocketName.data(), kSocketName.size());
    return static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 +
                                  kSocketName.size());
}

enum class Command { None, Settings, ZoneTest, Uninstall };

Command ParseArguments(int argc, char** argv, bool* help, bool* version) {
    Command command = Command::None;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            *help = true;
        } else if (argument == "--version" || argument == "-V") {
            *version = true;
        } else if (argument == "--settings") {
            command = Command::Settings;
        } else if (argument == "--zone-test") {
            command = Command::ZoneTest;
        } else if (argument == "--uninstall") {
            command = Command::Uninstall;
        }
    }
    return command;
}

void PrintUsage() {
    std::printf(
        "Smart Trackpad %s\n\n"
        "Usage: smarttrackpad [options]\n\n"
        "  --settings    edit the configuration in $EDITOR and reload it\n"
        "  --zone-test   print live contact positions until interrupted\n"
        "  --uninstall   remove the autostart entry\n"
        "  --version     print the version and exit\n"
        "  --help        print this help\n\n"
        "Configuration: %s\n",
        SMARTTRACKPAD_VERSION_TEXT, st::GetConfigPath().string().c_str());
}

// Sends a command to the running instance.  Returns true when one answered.
bool ForwardCommand(int command) {
    const int socketFd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socketFd < 0) {
        return false;
    }

    sockaddr_un address{};
    const socklen_t length = BuildSocketAddress(&address);

    const ssize_t sent =
        sendto(socketFd, &command, sizeof(command), 0,
               reinterpret_cast<const sockaddr*>(&address), length);
    close(socketFd);
    return sent == static_cast<ssize_t>(sizeof(command));
}

} // namespace

int main(int argc, char** argv) {
    bool help = false;
    bool version = false;
    const Command command = ParseArguments(argc, argv, &help, &version);

    if (help) {
        PrintUsage();
        return 0;
    }
    if (version) {
        std::printf("%s\n", SMARTTRACKPAD_VERSION_TEXT);
        return 0;
    }

    st::InitLogging(st::GetDataDirectory() / "smarttrackpad.log", false);
    st::InstallCrashHandler();
    st::LogFormat(st::LogLevel::Info, L"Smart Trackpad %ls starting.",
                  SMARTTRACKPAD_VERSION);
    st::LogFormat(st::LogLevel::Info, L"Configuration: %ls",
                  st::text::PathToWide(st::GetConfigPath()).c_str());

    if (command == Command::Uninstall) {
        std::error_code error;
        const char* home = std::getenv("HOME");
        if (home != nullptr) {
            std::filesystem::remove(std::filesystem::path(home) / ".config" /
                                        "autostart" / "smarttrackpad.desktop",
                                    error);
        }
        st::LogMessage(st::LogLevel::Info, L"Uninstall: autostart entry removed.");
        st::ShutdownLogging();
        return 0;
    }

    if (command == Command::Settings) {
        const st::AppConfig config = st::LoadConfig(st::GetConfigPath());
        st::ShutdownLogging();
        return st::RunSettingsEditor(config);
    }

    if (command == Command::ZoneTest) {
        const st::AppConfig config = st::LoadConfig(st::GetConfigPath());
        const int result = st::RunZoneTest(config);
        st::ShutdownLogging();
        return result;
    }

    if (!st::HasInteractiveSession()) {
        st::LogMessage(st::LogLevel::Error,
                       L"No graphical session (DISPLAY/WAYLAND_DISPLAY unset).");
        st::ShutdownLogging();
        return 1;
    }

    // Single instance: the socket name is the lock.
    const int listener = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (listener < 0) {
        st::LogMessage(st::LogLevel::Error, L"Could not create the command socket.");
        st::ShutdownLogging();
        return 1;
    }

    sockaddr_un address{};
    const socklen_t length = BuildSocketAddress(&address);

    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), length) != 0) {
        // Someone else owns the socket: hand the request over and exit.
        if (command != Command::None) {
            ForwardCommand(static_cast<int>(command));
        }
        close(listener);
        st::LogMessage(st::LogLevel::Info,
                       L"Another instance is running; request forwarded.");
        st::ShutdownLogging();
        return 0;
    }

    if (!st::HasInteractiveSession()) {
        st::LogMessage(st::LogLevel::Error,
                       L"No graphical session (DISPLAY/WAYLAND_DISPLAY unset).");
        close(listener);
        st::ShutdownLogging();
        return 1;
    }

    st::LinuxApp app;
    if (!app.Initialize()) {
        st::LogMessage(st::LogLevel::Error,
                       L"Startup failed; see the messages above.");
        close(listener);
        st::ShutdownLogging();
        return 1;
    }

    const int exitCode = app.Run();
    close(listener);
    st::ShutdownLogging();
    return exitCode;
}
