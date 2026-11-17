#include "IniFile.h"

#include "Text.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace st {
namespace {

using Section = std::map<std::wstring, std::wstring>;
using Document = std::map<std::wstring, Section>;

// Decodes the raw file bytes.  UTF-16 needs a BOM to be recognized; everything
// else is treated as UTF-8 (a BOM is optional).
std::wstring Decode(const std::vector<unsigned char>& bytes) {
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        std::wstring text;
        text.reserve((bytes.size() - 2) / 2);
        for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
            const char16_t unit =
                static_cast<char16_t>(bytes[index] | (bytes[index + 1] << 8));
            if constexpr (sizeof(wchar_t) == 2) {
                text.push_back(static_cast<wchar_t>(unit));
            } else {
                text.push_back(static_cast<wchar_t>(unit));
            }
        }
        return text;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        std::wstring text;
        text.reserve((bytes.size() - 2) / 2);
        for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
            text.push_back(static_cast<wchar_t>((bytes[index] << 8) | bytes[index + 1]));
        }
        return text;
    }

    std::size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        start = 3;
    }

    return text::ToWide(std::string_view(
        reinterpret_cast<const char*>(bytes.data()) + start, bytes.size() - start));
}

std::optional<std::wstring> ReadFileBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        return std::nullopt;
    }
    return Decode(bytes);
}

Document ParseDocument(const std::wstring& text) {
    Document document;
    std::wstring currentSection;

    std::size_t position = 0;
    while (position <= text.size()) {
        std::size_t lineEnd = text.find(L'\n', position);
        if (lineEnd == std::wstring::npos) {
            lineEnd = text.size();
        }

        std::wstring_view line(text.data() + position, lineEnd - position);
        position = lineEnd + 1;

        // Strip a trailing comment and surrounding whitespace.  A ';' or '#'
        // inside a value is not supported, which matches the Windows profile
        // API this replaces.
        for (const wchar_t marker : {L';', L'#'}) {
            const std::size_t found = line.find(marker);
            if (found != std::wstring_view::npos) {
                line = line.substr(0, found);
            }
        }
        line = text::TrimAscii(line);
        if (line.empty()) {
            continue;
        }

        if (line.front() == L'[' && line.back() == L']') {
            currentSection =
                text::LowerAscii(text::TrimAscii(line.substr(1, line.size() - 2)));
            document[currentSection];
            continue;
        }

        const std::size_t equals = line.find(L'=');
        if (equals == std::wstring_view::npos) {
            continue;
        }

        const std::wstring key =
            text::LowerAscii(text::TrimAscii(line.substr(0, equals)));
        const std::wstring value(text::TrimAscii(line.substr(equals + 1)));
        if (!key.empty()) {
            document[currentSection][key] = value;
        }
    }

    return document;
}

struct Cache {
    std::mutex mutex;
    std::filesystem::path path;
    bool loaded = false;
    // A cached parse is only valid while the file has the same size and
    // modification time; an editor (or the user) may rewrite it at any moment
    // and the reload must see the new contents, not the old document.
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
    Document document;
};

Cache& GetCache() {
    static Cache cache;
    return cache;
}

// Caller holds the cache mutex.
const Document& LoadCached(const std::filesystem::path& path) {
    Cache& cache = GetCache();

    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    const auto modified = error ? std::filesystem::file_time_type{}
                                : std::filesystem::last_write_time(path, error);

    if (cache.loaded && cache.path == path && !error && cache.size == size &&
        cache.modified == modified) {
        return cache.document;
    }

    cache.document = Document{};
    if (const std::optional<std::wstring> text = ReadFileBytes(path)) {
        cache.document = ParseDocument(*text);
    }
    cache.path = path;
    cache.loaded = !error || std::filesystem::exists(path);
    cache.size = error ? 0 : size;
    cache.modified = error ? std::filesystem::file_time_type{} : modified;
    return cache.document;
}

} // namespace

std::wstring ReadProfileString(const wchar_t* section, const wchar_t* key,
                               const wchar_t* fallback,
                               const std::filesystem::path& path) {
    Cache& cache = GetCache();
    std::lock_guard lock(cache.mutex);
    const Document& document = LoadCached(path);

    const auto sectionIt = document.find(text::LowerAscii(section));
    if (sectionIt == document.end()) {
        return fallback;
    }

    const auto valueIt = sectionIt->second.find(text::LowerAscii(key));
    if (valueIt == sectionIt->second.end()) {
        return fallback;
    }
    return valueIt->second;
}

bool HasProfileSection(const wchar_t* section, const std::filesystem::path& path) {
    Cache& cache = GetCache();
    std::lock_guard lock(cache.mutex);
    const Document& document = LoadCached(path);
    return document.find(text::LowerAscii(section)) != document.end();
}

bool WriteUtf8FileAtomic(const std::filesystem::path& path, const std::wstring& text) {
    std::filesystem::path temporary = path;
    temporary += L".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }

        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        file.write(reinterpret_cast<const char*>(bom), sizeof(bom));

        const std::string utf8 = text::ToUtf8(text);
        file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        file.flush();
        if (!file.good()) {
            std::error_code cleanup;
            std::filesystem::remove(temporary, cleanup);
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        // Windows refuses to rename onto an existing file; replace it instead.
        std::error_code removeError;
        std::filesystem::remove(path, removeError);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) {
        std::error_code cleanup;
        std::filesystem::remove(temporary, cleanup);
        return false;
    }

    // The cache now describes the previous contents.
    Cache& cache = GetCache();
    std::lock_guard lock(cache.mutex);
    cache.loaded = false;
    return true;
}

} // namespace st
