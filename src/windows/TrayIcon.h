#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace st {

struct TrayCallbacks {
    std::function<void()> onToggleEnabled;
    std::function<void()> onToggleBrightness;
    std::function<void()> onOpenSettings;
    std::function<void()> onOpenZoneTest;
    std::function<void()> onReloadConfig;
    std::function<void()> onOpenConfig;
    std::function<void()> onOpenLog;
    std::function<void()> onCopyLogPath;
    std::function<void()> onOpenDataFolder;
    std::function<void()> onToggleStartWithWindows;
    std::function<void()> onAbout;
    std::function<void()> onUninstall;
    std::function<void()> onExit;
    // Command forwarded by a second instance (see WindowNames.h).
    std::function<void(unsigned command)> onExternalCommand;
};

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HINSTANCE instance, const std::wstring& title, TrayCallbacks callbacks);
    void SetEnabled(bool enabled);
    void SetStartWithWindows(bool enabled);
    void SetBrightnessEnabled(bool enabled);
    void Remove();

private:
    static constexpr UINT kCallbackMessage = WM_APP + 1;

    static constexpr UINT kToggleCommand = 1001;
    static constexpr UINT kSettingsCommand = 1002;
    static constexpr UINT kZoneTestCommand = 1003;
    static constexpr UINT kReloadCommand = 1004;
    static constexpr UINT kOpenConfigCommand = 1005;
    static constexpr UINT kOpenLogCommand = 1006;
    static constexpr UINT kCopyLogCommand = 1007;
    static constexpr UINT kOpenDataCommand = 1008;
    static constexpr UINT kStartupCommand = 1009;
    static constexpr UINT kAboutCommand = 1010;
    static constexpr UINT kUninstallCommand = 1011;
    static constexpr UINT kExitCommand = 1012;
    static constexpr UINT kBrightnessCommand = 1013;

    static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                          LPARAM lParam);
    LRESULT WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void BuildTooltip();
    void AddOrUpdateIcon(bool add);
    void ShowMenu();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HICON icon_ = nullptr;
    UINT taskbarCreated_ = 0;
    UINT commandMessage_ = 0;
    bool added_ = false;
    bool enabled_ = true;
    bool startWithWindows_ = true;
    bool brightnessEnabled_ = false;
    std::wstring title_ = L"Smart Trackpad";
    std::wstring tooltip_;
    TrayCallbacks callbacks_;
};

} // namespace st
