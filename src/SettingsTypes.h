#pragma once

// Small enums shared between the configuration parser and the components that
// consume them, kept in their own header so neither has to include the other.

namespace st {

// Which HID digitizer collections the input layer listens to.  A touchscreen
// laptop should not change the system volume when the screen edge is swiped,
// so the default is the touchpad only.
enum class DigitizerDevices {
    Touchpad,
    Touchscreen,
    Both,
};

// Which displays the brightness edge drives.
enum class BrightnessTargets {
    All,        // every DDC/CI monitor plus an internal panel through WMI
    Primary,    // only the monitor on the primary display (DDC/CI)
    Internal,   // only an internal panel (WMI)
};

} // namespace st
