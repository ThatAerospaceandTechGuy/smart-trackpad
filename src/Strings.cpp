#include "Strings.h"

#include "Resources.h"

#include <windows.h>

#include <iterator>

namespace st {
namespace {

struct Fallback {
    unsigned id;
    const wchar_t* text;
};

// Keep in sync with the STRINGTABLE in Resources.rc.  The fallbacks only apply
// when the resource cannot be loaded (for example a stripped test binary).
constexpr Fallback kFallbacks[] = {
    {IDS_APP_NAME, L"Smart Trackpad"},
    {IDS_TRAY_ENABLED, L"enabled"},
    {IDS_TRAY_DISABLED, L"disabled"},
    {IDS_MENU_EDGE_GESTURES, L"Edge gestures"},
    {IDS_MENU_SETTINGS, L"Settings..."},
    {IDS_MENU_ZONE_TEST, L"Zone test..."},
    {IDS_MENU_RELOAD, L"Reload configuration"},
    {IDS_MENU_OPEN_CONFIG, L"Open configuration..."},
    {IDS_MENU_OPEN_LOG, L"Open log..."},
    {IDS_MENU_COPY_LOG_PATH, L"Copy log path"},
    {IDS_MENU_OPEN_DATA_FOLDER, L"Open data folder"},
    {IDS_MENU_START_WITH_WINDOWS, L"Start with Windows"},
    {IDS_MENU_ABOUT, L"About Smart Trackpad"},
    {IDS_MENU_UNINSTALL, L"Uninstall..."},
    {IDS_MENU_EXIT, L"Exit"},
    {IDS_ABOUT_FORMAT,
     L"Smart Trackpad %s\r\n\r\nConfiguration:\r\n%s\r\n\r\nLog file:\r\n%s"},
    {IDS_UNINSTALL_CONFIRM,
     L"Remove the Startup shortcut and the installed copy of Smart Trackpad?"
     L"\r\n\r\nYour configuration and log files are kept in:\r\n%s"},
    {IDS_UNINSTALL_FAILED,
     L"Smart Trackpad could not remove every file. See the log for details."},
    {IDS_COPY_FAILED, L"Could not copy the log path to the clipboard."},
    {IDS_COPY_DONE, L"Log path copied to the clipboard."},
    {IDS_NO_DIGITIZER,
     L"No absolute touchpad was found, so edge gestures are inactive."
     L"\r\n\r\nOpen the tray menu and choose \"Zone test...\" to see what "
     L"Windows reports, and check the log at:\r\n%s"},
    {IDS_ZONE_TEST_TITLE, L"Smart Trackpad - Zone test"},
    {IDS_ZONE_TEST_HELP,
     L"Touch the edges of the touchpad. The dot is the contact Windows reports."},
    {IDS_ZONE_TEST_LEFT, L"left = volume"},
    {IDS_ZONE_TEST_RIGHT, L"right = brightness"},
    {IDS_ZONE_TEST_TOP, L"top = media"},
    {IDS_ZONE_TEST_CONTACT, L"contact  x %.2f   y %.2f"},
    {IDS_ZONE_TEST_NO_CONTACT, L"no contact"},
};

} // namespace

std::wstring LoadString(unsigned id) {
    wchar_t buffer[512]{};
    const int length = ::LoadStringW(GetModuleHandleW(nullptr), id, buffer,
                                     static_cast<int>(std::size(buffer)));
    if (length > 0) {
        return std::wstring(buffer, static_cast<std::size_t>(length));
    }

    for (const Fallback& entry : kFallbacks) {
        if (entry.id == id) {
            return entry.text;
        }
    }
    return std::wstring();
}

} // namespace st
