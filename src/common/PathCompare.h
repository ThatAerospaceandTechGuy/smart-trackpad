#pragma once

#include <filesystem>

namespace st {

// True when both paths refer to the same file: a case-insensitive lexical match
// or, when both exist, the same file identity (so a junction or a different
// drive letter mapping also matches).
[[nodiscard]] bool SamePath(const std::filesystem::path& left,
                            const std::filesystem::path& right);

// True when `source` was modified strictly after `target`.  False when either
// file is missing or its timestamp cannot be read.
[[nodiscard]] bool SourceIsNewer(const std::filesystem::path& source,
                                 const std::filesystem::path& target);

} // namespace st
