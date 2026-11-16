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

A compact Windows 11-style overlay appears at the bottom center while either
absolute edge is adjusted. It shows the matching speaker or sun icon, a live
accent-colored level bar, and the percentage, then fades after two seconds. The
overlay is click-through, DPI-scaled, and follows the system light/dark theme
and accent color. It is drawn by the app because Windows exposes no public API
for showing its native brightness HUD.

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

The gesture state machine has no Win32 dependencies and ships with a small
self-contained test binary:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

(If CMake is not on `PATH`, use the `ctest.exe` from the same Visual Studio
directory as `cmake.exe`.)

## Run

Start `SmartTrackpad.exe` (no administrator rights required). On launch it
ensures a managed copy exists at
`%LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.exe`, updates that copy when the
launched executable is newer, and repairs the per-user Startup shortcut. The
shortcut and all data are per-user; no elevation is requested. A tray icon
appears with:

* **Edge gestures** - enable/disable without exiting.
* **Open configuration...** - edit the INI and restart the program.
* **Open log...** - diagnostics from the last run.
* **Exit**.

Only one instance can run at a time (a local named mutex enforces this).

## Configuration

Created on first launch at:

```
%LOCALAPPDATA%\SmartTrackpad\SmartTrackpad.ini
```

Log file: `%LOCALAPPDATA%\SmartTrackpad\smarttrackpad.log` (rotated at 1 MiB).
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
activation=2.0         ; percent of travel before a zone is recognized
step=10.0              ; horizontal travel per media step
invert_volume=0        ; 100% at the bottom, 0% at the top
invert_brightness=0    ; 100% at the bottom, 0% at the top
invert_media=0

[input]
debug=0                ; verbose HID/action diagnostics in the log
```

There is no volume- or brightness-step setting: both vertical edges map their
absolute position to 0-100%. `step` applies only to the top media edge. An
older `[brightness]` section is ignored, so an existing INI remains valid.

## Implementation

```
WM_INPUT  ->  RawInputSource  ->  GestureEngine  ->  ActionDispatcher
              (HID parsing)      (zone logic)       (Core Audio / DDC-WMI / media keys)
```

* **`App`** creates the required hidden message-only window (`HWND_MESSAGE`)
  and runs a single `GetMessage` loop. There is no polling: input arrives as
  `WM_INPUT`, device changes as `WM_INPUT_DEVICE_CHANGE`.
* **`RawInputSource`** registers the HID top-level collections for Touch Pad
  and Touch Screen (usage page `0x0D`, usages `0x05` / `0x04`) with
  `RIDEV_INPUTSINK | RIDEV_DEVNOTIFY`. Each device's preparsed data is cached
  once; reports are decoded with `HidP_GetUsageValue` (X/Y, contact count) and
  `HidP_GetUsages` (tip switches). Coordinates are normalized with the logical
  minimum/maximum from the HID value capabilities, including sign-extension
  for signed ranges. The HID Y axis is inverted so the engine receives
  bottom-up coordinates.
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
* **`BrightnessController`** uses the same asynchronous pattern. It first
  enumerates physical monitors with `EnumDisplayMonitors` +
  `GetPhysicalMonitorsFromHMONITOR` and uses `SetMonitorBrightness`. If no
  monitor supports DDC/CI (typical for a laptop's internal panel), it connects
  to WMI `root\wmi` and calls
  `WmiMonitorBrightnessMethods::WmiSetBrightness`. COM is initialized and
  released entirely on that worker thread.
* **`ActionDispatcher`** injects media keys with `SendInput` and forwards
  absolute volume/brightness values to their controllers and the overlay. Slow
  audio/display operations never run on the message loop.
* **`Osd`** owns a layered, click-through topmost window modeled on the
  measured Windows 11 hardware indicator (192x48 DIP card, 8 DIP corners,
  live accent fill, value text). It reads the system theme and accent palette
  so it follows Personalization changes, uses per-monitor DPI scaling, and
  fades with a short timer. It shows no window while idle and adds no polling
  or worker thread.
* **`Log`** is a mutex-protected UTF-8 file logger (also mirrored to
  `OutputDebugString`); debug logs are gated by the `debug` setting.

RAII is used for the raw input registration, device preparsed buffers,
physical monitor handles, COM/Core Audio interfaces, the single-instance
mutex, the controllers' worker threads and the tray icon. No network access
and no third-party dependencies.

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
  the requested level, but Windows' own volume flyout is not invoked. If no
  render endpoint exists, the log records a warning and the left edge has no
  effect. Mute state is left untouched.
* **The overlay is an app-drawn clone.** It matches the Windows 11 indicator's
  geometry and visual treatment, but it is not a system HUD. Windows provides
  no supported method to display the native brightness indicator after an
  application changes the level; this is why the app renders its own.
* **Brightness has two backends.** External monitors that implement MCCS are
  controlled through DDC/CI. Internal laptop panels usually reject DDC/CI, so
  the WMI `WmiMonitorBrightnessMethods` provider is used instead. If neither
  backend is available the log contains a warning and the right edge has no
  visible effect. WMI writes can be slower than DDC/CI; the worker coalesces
  rapid slider movement.
* **No automatic driver installation.** The utility installs only its own
  per-user executable and Startup shortcut. It cannot safely install the
  correct signed touchpad driver for every laptop, and it deliberately does
  not try to. A laptop whose driver exposes only relative mouse input remains
  unsupported unless the OEM provides an absolute digitizer interface.
* **Media key injection.** `SendInput` media keys can be ignored by a locked
  desktop or by applications that handle keys exclusively. The utility does
  not need elevation and does not install a driver or service.
* **Not verified against a live edge swipe here.** The build and gesture unit
  tests were run locally, and the Core Audio volume and WMI brightness setters
  were exercised on the development laptop; actual touchpad reports still
  depend on the HID descriptor supplied by the device. Enable `debug=1` and
  inspect the log to see detected devices, usage pages, X/Y ranges, and the
  volume/brightness backend in use.
* The tray uses a separate hidden top-level window because the notification
  area interacts poorly with message-only windows; the raw input window is
  message-only as designed.
