# Changelog

All notable changes to Smart Trackpad are documented here.  The project follows
[Semantic Versioning](https://semver.org/).

## [1.3.0]

### Added

- **macOS** port: menu bar item, multitouch input (private
  `MultitouchSupport`), Core Audio volume, `DisplayServices` brightness, AppKit
  overlay, LaunchAgent login item, `--settings` / `--zone-test` / `--uninstall`.
- **Linux** port: evdev absolute/multitouch input, PipeWire or PulseAudio volume,
  `/sys/class/backlight` brightness with a `brightnessctl` fallback, X11 overlay,
  XDG autostart, `--settings` / `--zone-test` / `--uninstall`, and a
  `--zone-test` live contact dump.
- CI matrix for Windows, Linux and macOS; `tools/build-linux.sh` and
  `tools/run-tests.sh` build and test with only a compiler.
- Portable crash handler for macOS/Linux (backtrace to the log and stderr).
- `evdev_decode` test binary: the Linux multitouch slot decoding (absolute
  ranges, y flip, multi-finger reports, slot reuse) is a tested unit instead of
  code that only runs on hardware.

### Changed

- The repository is split into `src/common` (portable core and tests),
  `src/windows`, `src/macos` and `src/linux`.  `GestureEngine`,
  `ContactTracker`, the configuration schema and the test binaries are shared by
  all three platforms.
- The configuration is read by a portable INI parser instead of
  `GetPrivateProfileStringW`, and is written as UTF-8 with a BOM.  UTF-16 files
  from earlier releases are still read.
- `ConfigWatcher` is per platform (`ReadDirectoryChangesW`, `inotify`, `kqueue`)
  and the INI cache now notices outside edits by size and modification time.

## [1.2.0]

### Added

- **Taps.** A short touch that never slides acts on the zone it started in:
  left edge toggles mute, top edge toggles playback.  A contact that crosses the
  activation distance is still a slider, and holding longer than 350 ms is not a
  tap.  There is no display-off gesture: it was triggered by accident far more
  often than on purpose.
- **Safety limits.** `[safety]` clamps the volume and brightness output range.
  The brightness floor defaults to 5% so the band can no longer black out a
  panel.
- **Brightness gestures are off by default.** Dragging the right edge maps the
  contact height to 0-100% brightness, and the bottom of the pad therefore means
  "as dark as the panel goes", which reads as the screen turning off.  The band
  is disabled in the shipped configuration and can be switched on from the tray
  menu or the settings dialog.
- **Cooldown.** After a cancelled gesture (second finger, device removal) the
  pad must settle for 150 ms, so a resting palm cannot re-trigger the edge.
- **Input device filter.** `[input] devices=touchpad|touchscreen|both`, default
  `touchpad`.  A touchscreen laptop no longer changes the system volume when the
  screen edge is swiped.
- **Brightness targets.** `[display] brightness_targets=all|primary|internal`.
- **Settings dialog** (tray menu, or `--settings`).  Sliders for the zones,
  gestures and limits, input and display choices, overlay and startup options,
  and *Restore defaults*.  Saving writes the INI and applies live.
- **Zone test window** (tray menu, or `--zone-test`): draws the pad, the
  configured bands and the live contact with its coordinates, so it is obvious
  whether the touchpad reports absolute positions and where the zones are.
- **Tray menu**: reload configuration, open configuration/log/data folder, copy
  log path, start with Windows toggle, about, uninstall.
- **Uninstall**: removes the Startup shortcut and the managed copy (the running
  executable is renamed and deleted at the next sign-in).
- **Crash reporting**: unhandled exceptions, `std::terminate`, pure virtual
  calls and invalid CRT parameters are logged and produce a minidump.
- **Command line**: `--settings` and `--zone-test`, forwarded to the running
  instance when one is already active, plus `--uninstall` (stops the running
  instance, removes the managed copy and the Startup shortcut).
- Stale `SmartTrackpad.exe.old` / `.new` copies from an in-use removal or an
  interrupted update are deleted on the next start, so uninstalling never needs
  administrator rights.
- Startup entries that launch a *different* `SmartTrackpad.exe` (an older copy
  in Downloads, for example) are detected and removed, so signing in cannot
  start a second, unrelated build.
- **Accessibility**: high-contrast colours, no fading when Windows animations
  are disabled, and `[osd] show_percentage=0` for a bar-only overlay.
- Localization: user-visible strings live in the `STRINGTABLE` in
  `src/Resources.rc`.

### Changed

- Brightness writes DDC/CI monitors **and** the WMI internal-panel provider
  instead of treating WMI as a mutually exclusive fallback.
- The overlay follows the monitor under the pointer and converges on the level
  the hardware reports (read-back, quantization, mute glyph).
- Configuration is reloaded live; older files are migrated once with their
  values preserved.
- A refreshed managed copy is launched and the old process hands over, so the
  running process is always the file the Startup shortcut points to.
- Logs rotate while running, not only at startup.
- Malformed INI numbers fall back to defaults instead of silently becoming 0.
- The session lock suspends gestures.

### Fixed

- The touchscreen collection no longer changes volume/brightness.
- Taps never change the display power state.
- The overlay no longer ignores the monitor the user is working on.

### Testing

- Five test binaries: `gesture_engine`, `hid_decode`, `contact_tracker`,
  `config`, `path_compare`.
- CI builds Debug and Release and runs the tests on every push (see
  `.github/workflows/build.yml`).

## [1.1.0]

### Added

- Application icon, `VERSIONINFO` block and an embedded PerMonitorV2 manifest.
- `hid_decode` and `config` test binaries.
- `.gitignore`, git history.

### Changed

- Overlay placement, applied-value feedback and mute glyph.
- Configuration watcher with live reload and legacy-schema migration.
- Installer handover: the process re-launches from the managed copy.

## [1.0.0]

Initial version: absolute volume and brightness sliders on the vertical
touchpad edges, media steps on the top edge, app-drawn Windows 11 style overlay,
per-user install, tray icon, INI configuration.
