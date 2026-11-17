#pragma once

#include "Config.h"

namespace st {

// `--zone-test`: prints what the input layer sees, one line per contact.  It
// runs without the single-instance lock and without touching volume or
// brightness, so it can be used while the service is running.
//
// This is the Linux answer to the zone test window on Windows: a desktop
// window would need a toolkit, and the numbers are what actually diagnose a
// missing or mis-scaled digitizer.
int RunZoneTest(const AppConfig& config);

// `--settings`: opens the configuration in $EDITOR.  The running instance
// reloads it through the file watcher.
int RunSettingsEditor(const AppConfig& config);

} // namespace st
