#pragma once

#include <filesystem>

namespace st {

// "Start at login": a per-user LaunchAgent in ~/Library/LaunchAgents.
//
// A LaunchAgent is used instead of the modern SMAppService API so the same code
// works on older systems and needs no code signing.
[[nodiscard]] std::filesystem::path AgentPlistPath();
[[nodiscard]] bool IsLoginItemEnabled();
bool SetLoginItem(bool enabled);

} // namespace st
