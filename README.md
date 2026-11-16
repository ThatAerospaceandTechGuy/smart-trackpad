# Smart Trackpad

A small, event-driven Windows background utility that turns the edges of a
Windows touchpad into hardware controls:

| Zone | Gesture | Action |
| --- | --- | --- |
| Left edge (0-10% width) | Touch/slide | Absolute volume: bottom = 0%, top = 100% |
| Right edge (90-100% width) | Touch/slide | Absolute brightness: bottom = 0%, top = 100% |
| Top edge (0-10% height) | Slide right / left | Next / previous track (one per 10% travel) |

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
can start. A gesture latches to its zone once activated, so the finger may
drift away from the edge while the control keeps working. Corners are
disambiguated by the dominant axis of the first movement: vertical in the
top-left corner controls volume, horizontal controls media.

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
the system light/dark theme and accent color. It is drawn by the app because
Windows exposes no public API for showing its native brightness HUD. The
overlay first shows the requested value (so the gesture feels immediate) and
then converges on what the hardware reports: a quantized step, a muted
endpoint, or a backend that refused the write.

## Build

Requirements: Windows 10/11, Visual Studio 2022+ (Desktop development with
C++), CMake 3.21+.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

The executable is `build\Release\SmartTrackpad.exe`. It is a `WIN32` GUI
subsystem binary and creates no visible main window - only a tray icon. The
Release build links the MSVC runtime statically, so it does not need a separate
Visual C++ redistributable.

The icon (`src/app.ico`, regenerate with `python tools/make_icon.py`), the
version block and the embedded manifest (PerMonitorV2 DPI awareness, long
paths, `asInvoker`) are compiled from `src/Resources.rc` and `src/app.manifest`.

Three self-contained test binaries ship with the project; the gesture engine,
the HID decoding helpers and the configuration parser have no Win32 UI
dependencies:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

| Test | Covers |
| --- | --- |
| `gesture_engine` | Zone selection, latching, absolute sliders, media steps, inversion |
| `hid_decode` | Sign extension, logical-range normalization, contact-state combination |
| `config` | Defaults, clamping, malformed values, legacy-schema migration |

(If CMake is not on `PATH`, use the `ctest.exe` from the same Visual Studio
directory as `cmake.exe`.)

## Run

Start `SmartTrackpad.exe` (no administrator rights required). On launch it
ensures a managed copy exists at
`%LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.exe`, updates that copy when the
launched executable is newer, repairs the per-user Startup shortcut, and then
**hands over to the managed copy** - so the process that keeps running is
always the file the Startup shortcut launches. All data is per-user; no
elevation is requested. A tray icon appears with:

* **Edge gestures** - enable/disable without exiting.
* **Open configuration...** - edit the INI; saving reloads it.
* **Open log...** - diagnostics from the last run.
* **Exit**.

Only one instance can run at a time (a local named mutex enforces this).

## Configuration

Created on first launch at:

```
%LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.ini
```

Log file: `%LOCALAPPDATA%\SmartTrackpad\smarttrackpad.log` (rotated to
`smarttrackpad.log.old` as soon as it passes 1 MiB, while running).
If `LOCALAPPDATA` is unavailable, both live under a `SmartTrackpad` folder next
to the executable.

```ini
[zones]
left_width=10          ; percent of touchpad width (1-45)
right_width=10
top_height=10          ; percent of touchpad height (1-45)
enable_left=1
enable_right=1
enable_top=1

[gestures]
activation=2.0         ; percent of travel before a zone is recognized (0.2-10)
step=10.0              ; horizontal travel per media step (2-50)
invert_volume=0        ; 100% at the bottom, 0% at the top
invert_brightness=0    ; 100% at the bottom, 0% at the top
invert_media=0

[input]
debug=0                ; verbose HID/action diagnostics in the log
```

Out-of-range values are clamped to the ranges above; values that are not
numbers fall back to the default instead of silently becoming zero. There is no
volume- or brightness-step setting: both vertical edges map their absolute
position to 0-100%. `step` applies only to the top media edge.

**Live reload.** Saving the file applies the new values to the running
instance; no restart is needed. Saves are debounced, so an editor that writes
the file several times produces one reload. A `debug` change takes effect
immediately because the log is re-opened.

**Migration.** Releases before 1.1.0 had a `[brightness] step_percent` section.
Such a file is rewritten once with the current template: every user value is
preserved, the obsolete section is removed, and the comments then describe the
current behaviour.

## Implementation

```
WM_INPUT  ->  RawInputSource  ->  GestureEngine  ->  ActionDispatcher
              (HID parsing)      (zone logic)       (Core Audio / DDC-WMI / media keys)
```

* **`App`** creates the required hidden message-only window (`HWND_MESSAGE`)
  and runs a single `GetMessage` loop. There is no polling: input arrives as
  `WM_INPUT`, device changes as `WM_INPUT_DEVICE_CHANGE`, and configuration
  changes as a message posted by the watcher.
* **`RawInputSource`** registers the HID top-level collections for Touch Pad
  and Touch Screen (usage page `0x0D`, usages `0x05` / `0x04`) with
  `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`. Each device's preparsed data is cached
  once; reports are decoded with `HidP_GetUsageValue` (X/Y, contact count) and
  `HidP_GetUsages` (tip switches). Coordinates are normalized with the logical
  minimum/maximum from the HID value capabilities, including sign-extension
  for signed ranges. The HID Y axis is inverted so the engine receives
  bottom-up coordinates. The numeric parts of that work live in
  **`HidDecode`** (`SignExtend`, `NormalizeValue`, `CombineContactSignals`) and
  are unit tested without a touchpad attached.
* **`GestureEngine`** is a pure state machine (`Idle`, `Pending`, `Active`,
  `Blocked`). It selects the zone from the starting point, requires the
  dominant axis to match the zone, then reports the absolute height for the
  left/right edges. The top edge discards activation travel and quantizes
  horizontal travel into media steps (capped per report to absorb HID
  glitches).
* **`VolumeController`** owns a `std::jthread` with a condition variable.
  It initializes COM as an MTA on that worker, opens the default multimedia
  render endpoint with `IMMDeviceEnumerator`, activates
  `IAudioEndpointVolume`, and applies
  `SetMasterVolumeLevelScalar`. Requests are coalesced; if the output device
  changes, the cached endpoint is dropped and re-opened on the next update.
  The level and mute state are read back so the overlay shows the real value.
* **`BrightnessController`** uses the same asynchronous pattern and drives
  **both** backends: every MCCS-capable monitor found with
  `EnumDisplayMonitors` + `GetPhysicalMonitorsFromHMONITOR` is set with
  `SetMonitorBrightness`, and the WMI `root\wmi`
  `WmiMonitorBrightnessMethods::WmiSetBrightness` provider is applied
  additionally for an internal laptop panel. A laptop with an external screen
  attached therefore moves both displays at once instead of only one of them.
  COM is initialized and released entirely on that worker thread.
* **`ConfigWatcher`** holds one overlapped `ReadDirectoryChangesW` on the data
  directory, posts a single reload per save (300 ms quiet period) and is
  stopped with a stop-token callback that wakes the pending read.
* **`ActionDispatcher`** injects media keys with `SendInput` and forwards
  absolute volume/brightness values to their controllers and the overlay. Slow
  audio/display operations never run on the message loop. Controller workers
  report the confirmed level back with `Osd::Post`, which marshals into the UI
  thread by `PostMessage`.
* **`Osd`** owns a layered, click-through topmost window modeled on the
  measured Windows 11 hardware indicator (192x48 DIP card, 8 DIP corners,
  live accent fill, value text). It reads the system theme and accent palette
  so it follows Personalization changes, uses per-monitor DPI scaling, follows
  the monitor under the pointer, and fades with a short timer. It shows no
  window while idle and adds no polling or worker thread.
* **`Log`** is a mutex-protected UTF-8 file logger (also mirrored to
  `OutputDebugString`); debug logs are gated by the `debug` setting and the
  file is rotated while running, not only at startup.

RAII is used for the raw input registration, device preparsed buffers,
physical monitor handles, COM/Core Audio interfaces, the single-instance
mutex, the controllers' worker threads, the watcher and the tray icon. No
network access and no third-party dependencies.

## Platform notes and limitations

* **Absolute contact data is required.** Coordinate zones only work when the
  touchpad exposes an absolute HID digitizer collection via Raw Input. Many
  Windows Precision Touchpads deliver single-finger motion to normal Win32
  applications only as relative mouse input, because Windows owns the PTP
  collection; this app deliberately does not guess zones from relative mouse
  deltas. On such systems the log records the registered collections, but no
  gestures are produced. Accessing those reports requires an OEM filter driver
  or a vendor API, which is outside a normal user-mode Win32 utility. Touch
  screens that expose HID digitizer reports are accepted as well.
* **Volume uses the default playback device.** Core Audio changes the system
  master volume without injecting volume keys; the app-drawn overlay reports
  the level that the endpoint actually holds (including a muted endpoint), but
  Windows' own volume flyout is not invoked. If no render endpoint exists, the
  log records a warning and the left edge has no effect. Mute state is left
  untouched.
* **The overlay is an app-drawn clone.** It matches the Windows 11 indicator's
  geometry and visual treatment, but it is not a system HUD. Windows provides
  no supported method to display the native brightness indicator after an
  application changes the level; this is why the app renders its own.
* **Brightness has two backends.** External monitors that implement MCCS are
  controlled through DDC/CI, and internal laptop panels through the WMI
  `WmiMonitorBrightnessMethods` provider; both are written for every change.
  If neither backend accepts the value the log contains a warning and the right
  edge has no visible effect. WMI writes can be slower than DDC/CI; the worker
  coalesces rapid slider movement.
* **No automatic driver installation.** The utility installs only its own
  per-user executable and Startup shortcut. It cannot safely install the
  correct signed touchpad driver for every laptop, and it deliberately does
  not try to. A laptop whose driver exposes only relative mouse input remains
  unsupported unless the OEM provides an absolute digitizer interface.
* **Media key injection.** `SendInput` media keys can be ignored by a locked
  desktop or by applications that handle keys exclusively. The utility does
  not need elevation and does not install a driver or service.
* **Not verified against a live edge swipe here.** The build and unit tests
  were run locally, and the Core Audio volume, WMI/DDC-CI brightness setters
  and the overlay were exercised on the development laptop; actual touchpad
  reports still depend on the HID descriptor supplied by the device. Enable
  `debug=1` and inspect the log to see detected devices, usage pages, X/Y
  ranges, reloads, and the volume/brightness backends in use.
* The tray uses a separate hidden top-level window because the notification
  area interacts poorly with message-only windows; the raw input window is
  message-only as designed.
