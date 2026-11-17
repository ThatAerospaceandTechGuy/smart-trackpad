#include "Text.h"

#include <cstdint>

namespace st::text {
namespace {

bool IsAsciiSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n' || character == '\f' || character == '\v';
}

} // namespace

std::wstring ToWide(std::string_view utf8) {
    std::wstring result;
    result.reserve(utf8.size());

    std::size_t index = 0;
    while (index < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[index]);
        std::uint32_t codePoint = 0;
        std::size_t extra = 0;

        if (lead < 0x80) {
            codePoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            codePoint = lead & 0x1F;
            extra = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            codePoint = lead & 0x0F;
            extra = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            codePoint = lead & 0x07;
            extra = 3;
        } else {
            // Invalid byte: keep it visible instead of dropping it.
            result.push_back(static_cast<wchar_t>(0xFFFD));
            ++index;
            continue;
        }

        if (index + extra >= utf8.size()) {
            result.push_back(static_cast<wchar_t>(0xFFFD));
            break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const auto continuation = static_cast<unsigned char>(utf8[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }

        if (!valid) {
            result.push_back(static_cast<wchar_t>(0xFFFD));
            ++index;
            continue;
        }

        index += extra + 1;

        if constexpr (sizeof(wchar_t) >= 4) {
            result.push_back(static_cast<wchar_t>(codePoint));
        } else if (codePoint <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(codePoint));
        } else {
            codePoint -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
        }
    }

    return result;
}

std::string ToUtf8(std::wstring_view wide) {
    std::string result;
    result.reserve(wide.size());

    for (std::size_t index = 0; index < wide.size(); ++index) {
        std::uint32_t codePoint = static_cast<std::uint32_t>(wide[index]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (codePoint >= 0xD800 && codePoint <= 0xDBFF && index + 1 < wide.size()) {
                const std::uint32_t low = static_cast<std::uint32_t>(wide[index + 1]);
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    codePoint = ((codePoint - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                    ++index;
                }
            }
        }

        if (codePoint < 0x80) {
            result.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
            result.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    return result;
}

std::wstring LowerAscii(std::wstring_view value) {
    std::wstring result(value);
    for (wchar_t& character : result) {
        if (character >= L'A' && character <= L'Z') {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }
    return result;
}

std::wstring_view TrimAscii(std::wstring_view value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && IsAsciiSpace(static_cast<char>(value[begin] & 0x7F))) {
        ++begin;
    }
    while (end > begin && IsAsciiSpace(static_cast<char>(value[end - 1] & 0x7F))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace st::text
