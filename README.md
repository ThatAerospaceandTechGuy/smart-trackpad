# Smart Trackpad

A small, event-driven background utility that turns the edges of a touchpad into
hardware controls.  It runs on **Windows**, **macOS** and **Linux** from one
shared gesture engine, one configuration file format and one set of tests.

| Platform | Input | Volume | Brightness | Overlay | Menu |
| --- | --- | --- | --- | --- | --- |
| Windows 10/11 | Raw Input HID digitizer | Core Audio | DDC/CI + WMI | GDI+ card | tray icon |
| macOS 12+ | MultitouchSupport (private) | Core Audio | DisplayServices (private) | AppKit panel | menu bar item |
| Linux (X11/Wayland) | evdev absolute axes | PipeWire/PulseAudio | `/sys/class/backlight` | X11 card | CLI + config file |

| Zone | Gesture | Action |
| --- | --- | --- |
| Left edge (0-10% width) | Touch/slide | Absolute volume: bottom = 0%, top = 100% |
| Left edge | Tap | Toggle mute |
| Right edge (90-100% width) | Touch/slide | Absolute brightness: bottom = 0%, top = 100% (off by default, see below) |
| Top edge (0-10% height) | Slide right / left | Next / previous track (one per 10% travel) |
| Top edge | Tap | Play / pause |

Coordinates are normalized to the touchpad surface:

```
                     top = 100%
        +-----------------------------------+
        |        media: prev / next         |  90-100% height
        |                                   |
        | volume                    bright  |
        | 0% .. 100%              0% .. 100% |  left/right 10% of width
        |                                   |
        |                                   |
        +-----------------------------------+
                    bottom = 0%
```

Vertical positions run from **0 at the bottom to 100 at the top**. HID
digitizers report Y downward internally, so the raw input layer flips it into
this bottom-up space before the gesture engine sees it.

Only one contact is tracked. A second finger cancels the gesture; after a
multi-finger report the pad must be fully released before a new edge gesture
can start, and a short cooldown stops a resting palm from re-triggering. A
gesture latches to its zone once activated, so the finger may drift away from
the edge while the control keeps working. Corners are disambiguated by the
dominant axis of the first movement: vertical in the top-left corner controls
volume, horizontal controls media, and a tap resolves to the vertical edge.

The brightness band is **off by default** (`[zones] enable_right=0`): touching
the bottom of the right edge used to take the panel to its darkest value, which
looks like the screen switching off. Turn it on in the tray menu (*Brightness
gestures*) or in the settings dialog when you want it; a short touch in that
band never switches the display off.

The left and right edges are **absolute sliders**. After the activation
threshold is crossed, the contact's height maps directly to 0-100% volume or
brightness: bottom = 0%, at the top = 100%. Touching at the same height again
does not resend an unchanged percentage. The top edge remains step-based:
each 10% of horizontal travel moves one media track. Both absolute controllers
run on worker threads and coalesce rapid changes, so a slow DDC/CI or Core Audio
transaction never stalls gesture recognition.

A compact Windows 11-style overlay appears at the bottom center of the display
the pointer is on while either absolute edge is adjusted. It shows the matching
speaker or sun icon, a live accent-colored level bar, and the percentage, then
fades after two seconds. The overlay is click-through, DPI-scaled, and follows
the system light/dark theme, accent color, high-contrast palette and the
"show animations" accessibility setting. It is drawn by the app because Windows
exposes no public API for showing its native brightness HUD. The overlay first
shows the requested value (so the gesture feels immediate) and then converges on
what the hardware reports: a quantized step, a muted endpoint, or a backend that
refused the write.

## Build

Requirements: CMake 3.21+ and one of

* **Windows**: Visual Studio 2022+ with "Desktop development with C++".
* **macOS**: Xcode command line tools (Xcode 15+).
* **Linux**: g++/clang++ and `libx11-dev` (without X11 the overlay is compiled
  out and only the level changes happen).

```powershell
# Windows
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

```sh
# macOS: build/SmartTrackpad.app
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Linux: build/smarttrackpad
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# ...or with nothing but a compiler:
sh tools/build-linux.sh && sh tools/run-tests.sh
```

The executable is `build\Release\SmartTrackpad.exe`. It is a `WIN32` GUI
subsystem binary and creates no visible main window - only a tray icon. The
Release build links the MSVC runtime statically, so it does not need a separate
Visual C++ redistributable.

The icon (`src/app.ico`, regenerate with `python tools/make_icon.py`), the
version block, the user-visible strings and the settings dialog template are
compiled from `src/Resources.rc`; the embedded manifest (PerMonitorV2 DPI
awareness, long paths, `asInvoker`) comes from `src/app.manifest`.

Five self-contained test binaries ship with the project; the gesture engine,
the HID decoding helpers, the contact lifecycle, the configuration parser and
the path helpers have no Win32 UI dependencies:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

| Test | Covers |
| --- | --- |
| `gesture_engine` | Zone selection, latching, absolute sliders, media steps, taps, cooldown, clamps, inversion |
| `hid_decode` | Sign extension, logical-range normalization, contact-state combination |
| `contact_tracker` | Down/move/up, multi-finger cancel, blocked-until-release, device removal |
| `config` | Defaults, clamping, malformed values, legacy-schema migration, UTF-16 files, save round-trip |
| `path_compare` | Path identity and timestamp comparison used by the installer handover |

`.github/workflows/build.yml` builds Windows (Debug and Release), Linux and
macOS, runs the tests on all three, and publishes a zip per platform on version
tags.  `tools/build-linux.sh` and `tools/run-tests.sh` do the same thing with
nothing but a compiler, which is how the Linux port was verified here.

## Run

Start `SmartTrackpad.exe` (no administrator rights required). On launch it
ensures a managed copy exists at
`%LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.exe`, updates that copy when the
launched executable is newer, repairs the per-user Startup shortcut (or removes
it, depending on the configuration), and then **hands over to the managed
copy** - so the process that keeps running is always the file the Startup
shortcut launches. All data is per-user; no elevation is requested.

Command-line switches are forwarded to the running instance when one exists:

```powershell
SmartTrackpad.exe --settings     # open the settings dialog
SmartTrackpad.exe --zone-test    # open the zone test window
```

The tray icon offers:

* **Edge gestures** - enable/disable without exiting.
* **Settings...** - edit everything with sliders instead of the INI file.
* **Zone test...** - see the pad, the bands and the live contact.
* **Reload configuration** / **Open configuration...** / **Open log...**
* **Copy log path** / **Open data folder**
* **Start with Windows** - create or remove the Startup shortcut.
* **About Smart Trackpad** - version and file locations.
* **Uninstall...** - remove the Startup shortcut and the installed copy.
* **Exit**.

Only one instance can run at a time (a local named mutex enforces this).
If the Startup folder contains another shortcut that launches a different
`SmartTrackpad.exe` - an old copy in `Downloads`, for example - it is logged and
removed, so signing in always starts the managed copy once.

### macOS

Open `build/SmartTrackpad.app` (or copy it to `/Applications`).  The program has
no Dock icon (`LSUIElement`); its menu bar glyph `◧` holds the menu:

* **Edge gestures** - enable/disable without quitting.
* **Reload configuration**, **Edit configuration…** (opens the INI in your
  default editor), **Open data folder**, **Open log**.
* **Start at login** - writes or removes
  `~/Library/LaunchAgents/dev.smarttrackpad.app.plist`.
* **About**, **Quit**.

Data lives in `~/Library/Application Support/SmartTrackpad/`.

```sh
SmartTrackpad.app/Contents/MacOS/SmartTrackpad --settings   # open the INI
SmartTrackpad.app/Contents/MacOS/SmartTrackpad --version
```

The first launch of a locally built app is blocked by Gatekeeper: use
*System Settings → Privacy & Security → Open Anyway*, or `xattr -d
com.apple.quarantine` on the bundle.  The input and brightness backends use
Apple's private frameworks, so the app cannot be distributed through the App
Store and will need re-verification after a major macOS release.

### Linux

```sh
./build/smarttrackpad              # systemd/autostart friendly, logs to the file
./build/smarttrackpad --zone-test  # live contact dump, Ctrl+C to stop
./build/smarttrackpad --settings   # open the INI in $VISUAL/$EDITOR
./build/smarttrackpad --uninstall  # remove the autostart entry
```

Data lives in `${XDG_DATA_HOME:-~/.local/share}/SmartTrackpad/`.  There is no
tray icon (that would add a GTK/SNI dependency): the configuration file is the
interface, and `--zone-test` is the diagnostic.

Running it at login is a normal XDG autostart entry,
`~/.config/autostart/smarttrackpad.desktop`, created when
`[general] start_with_windows=1`.

**Permissions.** Reading absolute touch coordinates needs access to
`/dev/input/event*`; add yourself to the `input` group
(`sudo usermod -aG input "$USER"`, then sign out and in).  Writing panel
brightness needs either a udev rule for `/sys/class/backlight/*/brightness` or
`brightnessctl` installed; the program detects both and logs which one it uses.
Volume goes through `wpctl` (PipeWire) or `pactl` (PulseAudio), whichever exists.

On a Wayland session the overlay is unavailable (there is no X11 display) and
the log says so; the volume and brightness changes still happen.

## Configuration

Created on first launch at:

```
Windows  %LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.ini
macOS    ~/Library/Application Support/SmartTrackpad/SmartTrackpad.ini
Linux    ${XDG_DATA_HOME:-~/.local/share}/SmartTrackpad/SmartTrackpad.ini
```

Log file: `smarttrackpad.log` in the same folder (rotated to
`smarttrackpad.log.old` as soon as it passes 1 MiB, while running). A crash on
Windows writes a `crash-<timestamp>.dmp` minidump next to the log; on macOS and
Linux the backtrace goes to the log and to stderr.
If the platform's data directory is unavailable, everything lives under a
`SmartTrackpad` folder next to the executable.

The file is UTF-8 with a BOM; UTF-16 files written by releases before 1.3.0 are
still read and are rewritten on the next save.

```ini
[zones]
left_width=10          ; percent of touchpad width (1-45)
right_width=10
top_height=10          ; percent of touchpad height (1-45)
enable_left=1
enable_right=0          ; brightness band is off by default
enable_top=1

[gestures]
activation=2.0         ; percent of travel before a zone is recognized (0.2-10)
step=10.0              ; horizontal travel per media step (2-50)
taps=1                 ; tap the left edge to mute, the top to play/pause
cooldown=150           ; ms before a gesture is accepted after a cancel (0-2000)
invert_volume=0        ; 100% at the bottom, 0% at the top
invert_brightness=0    ; 100% at the bottom, 0% at the top
invert_media=0

[safety]
min_volume=0           ; output clamps in percent
max_volume=100
min_brightness=5       ; keep a floor: many panels go black at 0
max_brightness=100

[display]
brightness_targets=all ; all | primary | internal

[input]
devices=touchpad       ; touchpad | touchscreen | both
debug=0                ; verbose HID/action diagnostics in the log

[osd]
show_percentage=1      ; 0 = icon and level bar only

[general]
start_with_windows=1   ; Windows: Startup shortcut, macOS: LaunchAgent, Linux: XDG autostart
config_version=2
```

Out-of-range values are clamped to the ranges above; values that are not
numbers fall back to the default instead of silently becoming zero. There is no
volume- or brightness-step setting: both vertical edges map their absolute
position to 0-100%. `step` applies only to the top media edge.

**Live reload.** Saving the file applies the new values to the running
instance; no restart is needed. Saves are debounced, so an editor that writes
the file several times produces one reload. A `debug` change takes effect
immediately because the log is re-opened. The settings dialog writes the same
file, so both ways of editing stay in sync.

**Migration.** Releases before 1.1.0 had a `[brightness] step_percent` section.
Such a file is rewritten once with the current template: every user value is
preserved, the obsolete section is removed, and the comments then describe the
current behaviour.

## Implementation

```
Windows: WM_INPUT  ->  RawInputSource  --macOS:   MT callback -> MultitouchInput --+->  ContactTracker  ->  GestureEngine  ->  platform actions
Linux:   evdev read  ->  EvdevInput    --/        (one contact)      (zone logic)
```

`src/common` holds everything that is not platform specific: `GestureEngine`,
`ContactTracker`, the configuration schema and INI parser, the absolute-value
helpers (`HidDecode`), logging, path rules and the per-user data locations.
`src/windows`, `src/macos` and `src/linux` hold the backends.  The five test
binaries link only `src/common`, so the behaviour is identical everywhere and
verified without hardware.

### Windows backends

* **`App`** creates the required hidden message-only window (`HWND_MESSAGE`)
  and runs a single `GetMessage` loop. There is no polling: input arrives as
  `WM_INPUT`, device changes as `WM_INPUT_DEVICE_CHANGE`, configuration changes
  as a message posted by the watcher, and session lock/unlock notifications
  from `WTSRegisterSessionNotification`.
* **`RawInputSource`** registers the requested HID top-level collections for
  Touch Pad and Touch Screen (usage page `0x0D`, usages `0x05` / `0x04`) with
  `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`. Each device's preparsed data is cached
  once; reports are decoded with `HidP_GetUsageValue` (X/Y, contact count) and
  `HidP_GetUsages` (tip switches). Coordinates are normalized with the logical
  minimum/maximum from the HID value capabilities, including sign-extension
  for signed ranges. The HID Y axis is inverted so the engine receives
  bottom-up coordinates. The numeric parts live in **`HidDecode`**
  (`SignExtend`, `NormalizeValue`, `CombineContactSignals`) and are unit tested
  without a touchpad attached.
* **`ContactTracker`** owns the contact lifecycle: one tracked contact, a
  multi-finger report cancels and blocks the device until it is fully released,
  a positionless report from a digitizer without a tip switch counts as a lift,
  and a second device cannot hijack an active gesture. Unit tested.
* **`GestureEngine`** is a pure state machine (`Idle`, `Pending`, `Active`,
  `Blocked`) with an injected clock. It selects the zone from the starting
  point, requires the dominant axis to match the zone, reports the absolute
  height for the left/right edges, quantizes top-edge travel into media steps
  (capped per report to absorb HID glitches), turns a short contact inside a
  band into a tap, and enforces the cooldown and output clamps.
* **`VolumeController`** owns a `std::jthread` with a condition variable.
  It initializes COM as an MTA on that worker, opens the default multimedia
  render endpoint with `IMMDeviceEnumerator`, activates
  `IAudioEndpointVolume`, and applies `SetMasterVolumeLevelScalar` (or flips
  `SetMute`). Requests are coalesced; if the output device changes, the cached
  endpoint is dropped and re-opened on the next update. The level and mute state
  are read back so the overlay shows the real value.
* **`BrightnessController`** uses the same asynchronous pattern and drives
  both backends: every MCCS-capable monitor found with `EnumDisplayMonitors` +
  `GetPhysicalMonitorsFromHMONITOR` is set with `SetMonitorBrightness`, and the
  WMI `root\wmi` `WmiMonitorBrightnessMethods::WmiSetBrightness` provider is
  applied additionally for an internal laptop panel. `[display]` narrows that
  to the primary display or the internal panel. COM is initialized and released
  entirely on that worker thread.
* **`ConfigWatcher`** holds one overlapped `ReadDirectoryChangesW` on the data
  directory, posts a single reload per save (300 ms quiet period) and is
  stopped with a stop-token callback that wakes the pending read.
* **`ActionDispatcher`** injects media keys with `SendInput`, toggles mute,
  and forwards absolute volume/brightness
  values to their controllers and the overlay. Slow audio/display operations
  never run on the message loop. Controller workers report the confirmed level
  back with `Osd::Post`, which marshals into the UI thread by `PostMessage`.
* **`Osd`** owns a layered, click-through topmost window modeled on the
  measured Windows 11 hardware indicator (192x48 DIP card, 8 DIP corners,
  live accent fill, value text). It reads the system theme and accent palette,
  falls back to the high-contrast system colors, uses per-monitor DPI scaling,
  follows the monitor under the pointer, and fades with a short timer (or snaps
  when animations are disabled).
* **`Settings`** is a modal dialog driven by a table of slider bindings; it
  reads and writes `AppConfig` and the caller saves the INI and applies the
  values live.
* **`Diagnostics`** is the zone test window: a double-buffered GDI canvas that
  draws the pad surface, the configured bands, the live contact and the counters
  fed by the gesture callbacks.
* **`Install`** manages the managed copy, the Startup shortcut and removal;
  **`PathCompare`** holds its file-identity and timestamp rules (unit tested).
* **`CrashHandler`** logs unhandled exceptions, `std::terminate`, pure calls and
  invalid CRT parameters and writes a minidump.
* **`Strings`** loads every user-visible string from the `STRINGTABLE` with an
  English fallback, so translating the product is a resource-only change.
* **`Log`** is a mutex-protected UTF-8 file logger (also mirrored to
  `OutputDebugString`) with a non-blocking variant for crash paths; debug logs
  are gated by the `debug` setting.

### macOS backends

* **`MultitouchInput`** loads the private `MultitouchSupport.framework` with
  `dlopen`, registers a contact-frame callback on the default device and maps
  the finger states to the same `ContactSample` the Windows path produces.  If
  the framework is missing or its symbols changed, the program logs why and
  keeps running without gestures.
* **`CoreAudioVolume`** reads and writes the default output device with
  `kAudioDevicePropertyVolumeScalar` / `kAudioDevicePropertyMute` (falling back
  to per-channel elements, then to an emulated mute by zeroing the level).
* **`MacBrightness`** uses `DisplayServicesGetBrightness` /
  `DisplayServicesSetBrightness` from the private `DisplayServices.framework`
  and reports whether the main display is the built-in panel, which is what
  `[display] brightness_targets` uses.
* **`OverlayMac`** is an `NSPanel` with a custom `NSView` drawing the same card,
  bar and value; updates are marshalled to the main queue and the panel hides
  itself after two seconds.
* **Menu bar item** (`NSStatusItem`) replaces the tray icon,
  `LoginItemMac` writes a LaunchAgent for "Start at login", and
  `ConfigWatcherMac` watches the configuration with `kqueue`.

### Linux backends

* **`EvdevInput`** scans `/dev/input/event*`, keeps the devices with absolute
  `ABS_X/ABS_Y` or multitouch slots, uses `EVIOCGPROP` to tell a touchpad
  (indirect) from a touchscreen (direct), and decodes one `SYN_REPORT` at a
  time.  `[input] devices` selects which kind is accepted.  Nothing is polled:
  the event loop `poll()`s the device descriptors.
* **`LinuxVolumeController`** drives `wpctl` (PipeWire) or `pactl`
  (PulseAudio) from a worker thread that coalesces requests, and reads the level
  back for the overlay.
* **`LinuxBrightnessController`** writes `/sys/class/backlight/*/brightness`
  and falls back to `brightnessctl` when the node is not writable.
* **`X11Overlay`** is an override-redirect X11 window with the same card and
  bar; on a Wayland-only session it stays disabled and logs that once.
* **`--settings` / `--zone-test`** open the INI in `$VISUAL`/`$EDITOR` and print
  live contact positions, and `LinuxApp` installs an XDG autostart entry.

RAII is used for the raw input registration, device preparsed buffers,
physical monitor handles, COM/Core Audio interfaces, the single-instance
mutex, the controllers' worker threads, the watcher, the tray icon and the X11
display. No network access and no third-party libraries: the audio and
brightness helpers on Linux call the tools that are already installed, and
everything else is a system framework.

## Platform notes and limitations

### Build status of this port

* **Windows** - built, unit tested and exercised on real hardware (the
  development laptop): volume, brightness, overlay, tray menu, settings dialog,
  zone test window, install handover, log rotation, live reload.
* **Linux** - built and unit tested (g++ 15, Ubuntu, WSL) and smoke tested
  (`--help`, `--version`, `--zone-test`, config creation, log output).  The evdev
  reader, `wpctl`/`pactl` calls and sysfs brightness writes follow the documented
  kernel/desktop interfaces but were not run against a physical touchpad in this
  environment (a VM has none).
* **macOS** - written against the documented and reverse-engineered interfaces
  (`MultitouchSupport`, `DisplayServices`, Core Audio, AppKit) and reviewed, but
  **not compiled or run here**: no Mac was available.  Expect to fix small
  compile errors on the first build; the file layout and the shared engine make
  that a mechanical exercise rather than a redesign.

### General

* **Absolute contact data is required.** Coordinate zones only work when the
  touchpad exposes an absolute HID digitizer collection via Raw Input. Many
  Windows Precision Touchpads deliver single-finger motion to normal Win32
  applications only as relative mouse input, because Windows owns the PTP
  collection; this app deliberately does not guess zones from relative mouse
  deltas. Use the zone test window to see what Windows reports. Accessing those
  reports requires an OEM filter driver or a vendor API, which is outside a
  normal user-mode Win32 utility. Touch screens are supported but disabled by
  default (`[input] devices`) so a screen edge swipe does not change the system
  volume.
* **Volume uses the default playback device.** Core Audio changes the system
  master volume without injecting volume keys; the app-drawn overlay reports
  the level that the endpoint actually holds (including a muted endpoint), but
  Windows' own volume flyout is not invoked. If no render endpoint exists, the
  log records a warning and the left edge has no effect. A tap toggles mute.
* **The overlay is an app-drawn clone.** It matches the Windows 11 indicator's
  geometry and visual treatment, but it is not a system HUD. Windows provides
  no supported method to display the native brightness indicator after an
  application changes the level; this is why the app renders its own.
* **Brightness has two backends.** External monitors that implement MCCS are
  controlled through DDC/CI, and internal laptop panels through the WMI
  `WmiMonitorBrightnessMethods` provider; both are written for every change
  unless `[display]` narrows it. If neither backend accepts the value the log
  contains a warning and the right edge has no visible effect.
* **No automatic driver installation.** The utility installs only its own
  per-user executable and Startup shortcut. It cannot safely install the
  correct signed touchpad driver for every laptop, and it deliberately does
  not try to.
* **Media key injection.** `SendInput` media keys can be ignored by a locked
  desktop or by applications that handle keys exclusively. Gestures are
  suspended while the session is locked.
* **Single contact only.** Two-finger fine adjustment is not implemented; a
  second finger always cancels the gesture.
* **macOS uses private frameworks.** `MultitouchSupport` and `DisplayServices`
  are not public API: they can change with any macOS release, and the app cannot
  be sandboxed or shipped through the App Store. Nothing is installed in a
  system location, but a future macOS version may require updating the resolved
  symbols.
* **Linux brightness needs privileges.** Writing
  `/sys/class/backlight/*/brightness` requires a udev rule or `brightnessctl`.
  DDC/CI monitors are not driven on Linux (that needs the i2c device nodes and a
  protocol implementation); on macOS and Windows external monitors are covered
  by DDC/CI.
* **Not verified against a live edge swipe everywhere.** Windows was exercised
  on the development laptop; the macOS and Linux input paths need a run on the
  target hardware. Enable `debug=1` and inspect the log (or use `--zone-test` on
  Linux) to see detected devices, ranges, reloads and the backends in use.
