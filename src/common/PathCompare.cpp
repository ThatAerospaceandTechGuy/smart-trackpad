#include "PathCompare.h"

#include <filesystem>
#include <system_error>

namespace st {

bool SamePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
#if defined(_WIN32)
    // Windows paths are case-insensitive.
    if (_wcsicmp(left.lexically_normal().c_str(),
                 right.lexically_normal().c_str()) == 0) {
        return true;
    }
#else
    if (left.lexically_normal() == right.lexically_normal()) {
        return true;
    }
#endif

    std::error_code error;
    if (std::filesystem::exists(left, error)) {
        error.clear();
        if (std::filesystem::exists(right, error)) {
            error.clear();
            return std::filesystem::equivalent(left, right, error) && !error;
        }
    }
    return false;
}

bool SourceIsNewer(const std::filesystem::path& source,
                   const std::filesystem::path& target) {
    std::error_code sourceError;
    std::error_code targetError;
    const auto sourceTime =
        std::filesystem::last_write_time(source, sourceError);
    const auto targetTime =
        std::filesystem::last_write_time(target, targetError);
    return !sourceError && !targetError && sourceTime > targetTime;
}

} // namespace st
