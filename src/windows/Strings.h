#pragma once

#include <string>

namespace st {

// Loads a user-visible string from the STRINGTABLE in Resources.rc.  Every id
// has an English fallback compiled into the binary, so a missing or partial
// resource file never produces an empty menu.
[[nodiscard]] std::wstring LoadString(unsigned id);

} // namespace st
