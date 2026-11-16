# Changelog

All notable changes to Smart Trackpad are documented here.  The project follows
[Semantic Versioning](https://semver.org/).

## [1.2.0]

### Added

- **Taps.** A short touch that never slides acts on the zone it started in:
  left edge toggles mute, top edge toggles playback, right edge turns the
  display off.  A contact that crosses the activation distance is still a
  slider, and holding longer than 350 ms is not a tap.
- **Safety limits.** `[safety]` clamps the volume and brightness output range.
  The brightness floor defaults to 5% so the right edge can no longer black out
  a panel.
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
- Taps on the right edge cannot leave the display dark with no way back
  (the brightness floor applies to taps as well).
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
