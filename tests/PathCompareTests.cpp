#include "PathCompare.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace {

#if defined(_WIN32)
#include <windows.h>
int GetProcessIdHint() {
    return static_cast<int>(GetCurrentProcessId());
}
#else
#include <unistd.h>
int GetProcessIdHint() {
    return static_cast<int>(getpid());
}
#endif

int g_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": CHECK failed: " #condition << '\n';                 \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

class TempDirectory {
public:
    TempDirectory() {
        static int counter = 0;
        std::error_code error;
        std::filesystem::path path =
            std::filesystem::temp_directory_path(error) / "SmartTrackpadTests";
        if (error) {
            path = "SmartTrackpadTests";
        }
        path /= std::to_string(++counter) + "-" + std::to_string(GetProcessIdHint());
        directory_ = path;
        std::filesystem::create_directories(directory_, error);
    }

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] std::filesystem::path File(const wchar_t* name) const {
        return directory_ / name;
    }

private:
    std::filesystem::path directory_;
};

void WriteFile(const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "x";
}

void TestSamePathLexical() {
#if defined(_WIN32)
    // Windows paths are case-insensitive.
    CHECK(st::SamePath(L"C:/Temp/App/app.exe", LR"(c:\temp\app\APP.EXE)"));
#else
    // POSIX paths are case-sensitive, so a different case is a
    // different file.
    CHECK(!st::SamePath(L"C:/Temp/App/app.exe", L"C:/Temp/App/APP.EXE"));
#endif
    CHECK(st::SamePath(L"C:/Temp/App/app.exe", L"C:/Temp/App/./app.exe"));
    CHECK(!st::SamePath(L"C:/Temp/App/app.exe", L"C:/Temp/Other/app.exe"));
}

void TestSamePathIdentity() {
    TempDirectory temp;
    const std::filesystem::path file = temp.File(L"copy.exe");
    WriteFile(file);

    // A relative path to the same file must compare equal even when the
    // spelled form differs.
    std::error_code error;
    const std::filesystem::path relative =
        std::filesystem::relative(file, std::filesystem::current_path(), error);
    if (!error) {
        CHECK(st::SamePath(file, relative));
    }

    CHECK(!st::SamePath(file, temp.File(L"missing.exe")));
}

void TestSourceIsNewer() {
    TempDirectory temp;
    const std::filesystem::path source = temp.File(L"source.exe");
    const std::filesystem::path target = temp.File(L"target.exe");
    WriteFile(source);
    WriteFile(target);

    const auto now = std::filesystem::file_time_type::clock::now();
    std::error_code error;

    std::filesystem::last_write_time(target, now - std::chrono::hours(1), error);
    CHECK(!error);
    std::filesystem::last_write_time(source, now, error);
    CHECK(!error);

    CHECK(st::SourceIsNewer(source, target));
    CHECK(!st::SourceIsNewer(target, source));

    // Equal timestamps are not "newer".
    std::filesystem::last_write_time(target, now, error);
    CHECK(!error);
    CHECK(!st::SourceIsNewer(source, target));

    // Missing files never look newer.
    CHECK(!st::SourceIsNewer(temp.File(L"absent.exe"), target));
    CHECK(!st::SourceIsNewer(source, temp.File(L"absent.exe")));
}

} // namespace

#define RUN_TEST(test)              \
    do {                            \
        std::cout << #test << '\n'; \
        test();                     \
    } while (false)

int main() {
    RUN_TEST(TestSamePathLexical);
    RUN_TEST(TestSamePathIdentity);
    RUN_TEST(TestSourceIsNewer);

    if (g_failures != 0) {
        std::cerr << g_failures << " check(s) failed.\n";
        return 1;
    }

    std::cout << "All path comparison tests passed.\n";
    return 0;
}
