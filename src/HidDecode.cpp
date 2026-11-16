#include "HidDecode.h"

#include <algorithm>

namespace st::hid {

std::int64_t SignExtend(std::uint32_t raw, std::uint16_t bitSize) {
    if (bitSize == 0 || bitSize >= 32) {
        return static_cast<std::int32_t>(raw);
    }

    const std::uint32_t signBit = 1u << (bitSize - 1);
    if ((raw & signBit) == 0) {
        return static_cast<std::int64_t>(raw);
    }
    const std::uint32_t mask = ~0u << bitSize;
    return static_cast<std::int32_t>(raw | mask);
}

float NormalizeValue(std::uint32_t raw, std::int32_t logicalMinimum,
                     std::int32_t logicalMaximum, std::uint16_t bitSize) {
    if (logicalMaximum <= logicalMinimum) {
        return 0.5f;
    }

    const std::int64_t value =
        logicalMinimum < 0 ? SignExtend(raw, bitSize)
                           : static_cast<std::int64_t>(raw);
    const float fraction =
        static_cast<float>(value - logicalMinimum) /
        static_cast<float>(static_cast<std::int64_t>(logicalMaximum) - logicalMinimum);
    return std::clamp(fraction, 0.0f, 1.0f);
}

Contact CombineContactSignals(const ContactSignals& signals) {
    Contact state = Contact::Unknown;

    switch (signals.tipQuery) {
    case TipQuery::BufferTooSmall:
        state = Contact::Multiple;
        break;
    case TipQuery::Ok:
        if (signals.tipCount <= 0) {
            state = Contact::Up;
        } else if (signals.tipCount > 1) {
            state = Contact::Multiple;
        } else {
            state = Contact::Down;
        }
        break;
    case TipQuery::Failed:
    case TipQuery::Unavailable:
        break;
    }

    if (signals.hasContactCount) {
        if (signals.contactCount == 0) {
            state = Contact::Up;
        } else if (signals.contactCount > 1) {
            state = Contact::Multiple;
        } else if (state == Contact::Unknown) {
            state = Contact::Down;
        }
    }

    return state;
}

} // namespace st::hid
