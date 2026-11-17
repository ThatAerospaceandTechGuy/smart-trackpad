#include "AppMac.h"

#include "Config.h"
#include "Text.h"
#include "CrashHandlerPosix.h"
#include "Log.h"
#include "LoginItemMac.h"
#include "Platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum class Command { None, Settings, ZoneTest, Uninstall };

Command ParseArguments(int argc, char** argv, bool* help, bool* version) {
    Command command = Command::None;
    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];
        if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0) {
            *help = true;
        } else if (std::strcmp(argument, "--version") == 0 ||
                   std::strcmp(argument, "-V") == 0) {
            *version = true;
        } else if (std::strcmp(argument, "--settings") == 0) {
            command = Command::Settings;
        } else if (std::strcmp(argument, "--zone-test") == 0) {
            command = Command::ZoneTest;
        } else if (std::strcmp(argument, "--uninstall") == 0) {
            command = Command::Uninstall;
        }
    }
    return command;
}

void PrintUsage() {
    std::printf("Smart Trackpad %s\n\n"
                "Usage: SmartTrackpad [options]\n\n"
                "  --settings    show the configuration in Finder\n"
                "  --zone-test   log every contact position (enable debug logging)\n"
                "  --uninstall   remove the login item\n"
                "  --version     print the version and exit\n"
                "  --help        print this help\n\n"
                "Configuration: %s\n",
                SMARTTRACKPAD_VERSION_TEXT, st::GetConfigPath().string().c_str());
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
        st::SetLoginItem(false);
        st::LogMessage(st::LogLevel::Info, L"Uninstall: login item removed.");
        st::ShutdownLogging();
        return 0;
    }

    if (command == Command::Settings) {
        const st::AppConfig config = st::LoadConfig(st::GetConfigPath());
        // Opening the file needs only the path; the full app would bring up the
        // input and audio stacks for nothing.
        const std::string editor = "open -t '" + config.configPath.string() + "'";
        (void)std::system(editor.c_str());
        st::ShutdownLogging();
        return 0;
    }

    // One instance owns the trackpad; the menu bar item is the control surface.
    // (A second launch simply opens no second status item.)
    st::MacApp app;
    if (command == Command::ZoneTest) {
        app.EnableContactLogging();
        st::LogMessage(st::LogLevel::Info,
                       L"Zone test: logging every contact position.");
    }
    if (!app.Initialize()) {
        st::LogMessage(st::LogLevel::Error, L"Startup failed; see the messages above.");
        st::ShutdownLogging();
        return 1;
    }

    const int exitCode = app.Run();
    st::ShutdownLogging();
    return exitCode;
}
