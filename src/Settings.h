#pragma once

#include <windows.h>

#include "Config.h"

namespace st {

// Shows the modal settings dialog.  Returns true when the user accepted the
// changes, in which case `config` holds the new values; the caller saves them
// and applies them to the running components.
bool ShowSettingsDialog(HINSTANCE instance, HWND owner, AppConfig& config);

} // namespace st
