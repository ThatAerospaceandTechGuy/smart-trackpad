#include "LoginItemMac.h"

#include "Log.h"
#include "Platform.h"
#include "Text.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace st {
namespace {

constexpr const char* kLabel = "dev.smarttrackpad.app";

std::filesystem::path HomeDirectory() {
    const char* home = std::getenv("HOME");
    return home == nullptr ? std::filesystem::path{} : std::filesystem::path(home);
}

} // namespace

std::filesystem::path AgentPlistPath() {
    const std::filesystem::path home = HomeDirectory();
    if (home.empty()) {
        return {};
    }
    return home / "Library" / "LaunchAgents" / (std::string(kLabel) + ".plist");
}

bool IsLoginItemEnabled() {
    const std::filesystem::path path = AgentPlistPath();
    std::error_code error;
    return !path.empty() && std::filesystem::exists(path, error);
}

bool SetLoginItem(bool enabled) {
    const std::filesystem::path path = AgentPlistPath();
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    if (!enabled) {
        std::filesystem::remove(path, error);
        LogMessage(LogLevel::Info, L"Start at login disabled.");
        return !error;
    }

    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        LogMessage(LogLevel::Warning, L"Could not write the LaunchAgent plist.");
        return false;
    }

    file << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
         << "<plist version=\"1.0\">\n"
         << "<dict>\n"
         << "    <key>Label</key>\n"
         << "    <string>" << kLabel << "</string>\n"
         << "    <key>ProgramArguments</key>\n"
         << "    <array>\n"
         << "        <string>" << ExecutablePath().string() << "</string>\n"
         << "    </array>\n"
         << "    <key>RunAtLoad</key>\n"
         << "    <true/>\n"
         << "    <key>ProcessType</key>\n"
         << "    <string>Interactive</string>\n"
         << "</dict>\n"
         << "</plist>\n";

    LogFormat(LogLevel::Info, L"Start at login enabled (\"%ls\").",
              text::PathToWide(path).c_str());
    return true;
}

} // namespace st
