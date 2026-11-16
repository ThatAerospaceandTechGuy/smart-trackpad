#pragma once

#include <cstdint>

// Small, Win32-free helpers that decode HID digitizer values.  They are split
// out of RawInputSource so the sign-extension, normalization and contact-state
// rules can be unit tested without a touchpad attached.
namespace st::hid {

// Signed value of a raw HID field, interpreting the low `bitSize` bits as a
// two's complement number.  A bit size of 0 or 32+ keeps the value as-is.
[[nodiscard]] std::int64_t SignExtend(std::uint32_t raw, std::uint16_t bitSize);

// Maps a raw absolute-axis value into 0.0..1.0 using the logical range from
// HIDP_VALUE_CAPS.  Signed ranges are sign-extended first.  A degenerate range
// (maximum <= minimum) yields 0.5 so callers never see a division by zero.
[[nodiscard]] float NormalizeValue(std::uint32_t raw, std::int32_t logicalMinimum,
                                   std::int32_t logicalMaximum,
                                   std::uint16_t bitSize);

// Result of the tip-switch query for one report.
enum class TipQuery {
    Unavailable,   // The device has no tip-switch usage page we can query.
    Failed,        // HidP_GetUsages returned an error other than "buffer small".
    Ok,            // `tipCount` tip switches are down in this report.
    BufferTooSmall, // More tip switches than the caller's buffer: many contacts.
};

enum class Contact {
    Unknown,  // The report carries no contact state; coordinates only.
    Up,       // Every contact is lifted.
    Down,     // Exactly one contact is down.
    Multiple, // More than one contact is down.
};

// Aggregates the two independent contact-state sources of a digitizer report.
// Contact Count (a value usage) wins over Tip Switch (button usages) when both
// are present, matching the Windows Precision Touchpad descriptor layout.
struct ContactSignals {
    TipQuery tipQuery = TipQuery::Unavailable;
    int tipCount = 0;
    bool hasContactCount = false;
    std::uint32_t contactCount = 0;
};

[[nodiscard]] Contact CombineContactSignals(const ContactSignals& signals);

} // namespace st::hid
