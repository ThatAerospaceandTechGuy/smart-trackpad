#pragma once

#include <filesystem>
#include <string>

namespace st {

// Reads "section/key" from an INI file the way GetPrivateProfileString used to:
// section names and keys are case-insensitive, values are trimmed, `;` and `#`
// start a comment and a missing key yields `fallback`.
//
// The file may be UTF-16 LE/BE (with BOM, what older releases wrote) or UTF-8
// with or without a BOM.  Results are cached for the last file that was read,
// because a single configuration load asks for dozens of values.
[[nodiscard]] std::wstring ReadProfileString(const wchar_t* section, const wchar_t* key,
                                             const wchar_t* fallback,
                                             const std::filesystem::path& path);

// True when the file contains the section (used to detect the legacy
// [brightness] section during migration).
[[nodiscard]] bool HasProfileSection(const wchar_t* section,
                                     const std::filesystem::path& path);

// Writes `text` as UTF-8 with a BOM through a temporary file and an atomic
// rename, so a watcher or a crash never sees a half-written file.
bool WriteUtf8FileAtomic(const std::filesystem::path& path, const std::wstring& text);

} // namespace st
