#pragma once

#include <string>
#include <string_view>

namespace st::text {

// UTF-8 <-> wide conversion.  On Windows `wchar_t` is UTF-16, elsewhere it is
// UTF-32; both directions accept either encoding on input.
[[nodiscard]] std::wstring ToWide(std::string_view utf8);
[[nodiscard]] std::string ToUtf8(std::wstring_view wide);

// Lowercases ASCII letters only.  Configuration keys, section names and the
// small set of enum words are ASCII, so this avoids a locale dependency.
[[nodiscard]] std::wstring LowerAscii(std::wstring_view value);

// Trims ASCII whitespace.
[[nodiscard]] std::wstring_view TrimAscii(std::wstring_view value);

} // namespace st::text
