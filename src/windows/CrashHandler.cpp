#include "CrashHandler.h"

#include "Config.h"
#include "Log.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdlib>
#include <cwchar>
#include <exception>
#include <string>

namespace st {
namespace {

HANDLE g_dumpFile = INVALID_HANDLE_VALUE;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

const wchar_t* ModuleNameFromAddress(void* address, std::wstring& storage) {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            static_cast<LPCWSTR>(address), &module) &&
        module != nullptr) {
        wchar_t buffer[MAX_PATH]{};
        if (GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer))) >
            0) {
            storage = buffer;
            return storage.c_str();
        }
    }
    return L"<unknown module>";
}

// A crash can happen while the log mutex is held, so the handler uses the
// best-effort writer that never blocks.
void Report(const std::wstring& text) {
    LogMessageBestEffort(LogLevel::Error, text);
}

void WriteMiniDump(EXCEPTION_POINTERS* exceptionPointers) {
    const std::filesystem::path directory = GetDataDirectory();
    std::error_code error;
    std::filesystem::create_directories(directory, error);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t name[64]{};
    swprintf(name, std::size(name), L"crash-%04u%02u%02u-%02u%02u%02u.dmp",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    const std::filesystem::path path = directory / name;

    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Report(L"Crash: could not create a minidump file (error " +
               std::to_wstring(GetLastError()) + L").");
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exceptionPointers;
    info.ClientPointers = FALSE;

    const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
        MiniDumpWithThreadInfo);

    if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          exceptionPointers != nullptr ? type : MiniDumpNormal,
                          exceptionPointers != nullptr ? &info : nullptr, nullptr,
                          nullptr)) {
        Report(L"Crash: minidump written to \"" + path.wstring() + L"\".");
    } else {
        Report(L"Crash: MiniDumpWriteDump failed (error " +
               std::to_wstring(GetLastError()) + L").");
    }

    CloseHandle(file);
}

LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) {
    std::wstring module;
    const DWORD code = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord
                           ? exceptionPointers->ExceptionRecord->ExceptionCode
                           : 0;
    void* address = exceptionPointers != nullptr && exceptionPointers->ExceptionRecord
                        ? exceptionPointers->ExceptionRecord->ExceptionAddress
                        : nullptr;

    Report(L"Crash: unhandled exception 0x" + [code] {
               wchar_t buffer[16]{};
               swprintf(buffer, std::size(buffer), L"%08lX",
                        static_cast<unsigned long>(code));
               return std::wstring(buffer);
           }() +
           L" at " + std::to_wstring(reinterpret_cast<uintptr_t>(address)) + L" in \"" +
           ModuleNameFromAddress(address, module) + L"\".");

    WriteMiniDump(exceptionPointers);

    // Let Windows Error Reporting continue with its own handling.
    if (g_previousFilter != nullptr) {
        return g_previousFilter(exceptionPointers);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void OnTerminate() {
    Report(L"Crash: std::terminate called (uncaught exception or noexcept "
           L"violation).");
    WriteMiniDump(nullptr);
    std::abort();
}

void OnPureCall() {
    Report(L"Crash: pure virtual function call.");
    WriteMiniDump(nullptr);
    std::abort();
}

void OnInvalidParameter(const wchar_t* expression, const wchar_t* function,
                        const wchar_t* file, unsigned int line, uintptr_t) {
    std::wstring text = L"Crash: invalid CRT parameter";
    if (function != nullptr) {
        text += L" in ";
        text += function;
    }
    if (file != nullptr) {
        text += L" (";
        text += file;
        text += L":";
        text += std::to_wstring(line);
        text += L")";
    }
    if (expression != nullptr) {
        text += L": ";
        text += expression;
    }
    Report(text + L".");
}

} // namespace

void InstallCrashHandler() {
    g_previousFilter = SetUnhandledExceptionFilter(&UnhandledExceptionFilter);
    std::set_terminate(&OnTerminate);
    _set_purecall_handler(&OnPureCall);
    _set_invalid_parameter_handler(&OnInvalidParameter);
}

} // namespace st
