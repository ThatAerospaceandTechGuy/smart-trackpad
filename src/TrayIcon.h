#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace st {

struct TrayCallbacks {
    std::function<void()> onToggleEnabled;
    std::function<void()> onOpenConfig;
    std::function<void()> onOpenLog;
    std::function<void()> onExit;
};

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HINSTANCE instance, const std::wstring& title, TrayCallbacks callbacks);
    void SetEnabled(bool enabled);
    void Remove();

private:
    static constexpr UINT kCallbackMessage = WM_APP + 1;
    static constexpr UINT kToggleCommand = 1001;
    static constexpr UINT kOpenConfigCommand = 1002;
    static constexpr UINT kOpenLogCommand = 1003;
    static constexpr UINT kExitCommand = 1004;

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
    bool added_ = false;
    bool enabled_ = true;
    std::wstring title_ = L"Smart Trackpad";
    std::wstring tooltip_;
    TrayCallbacks callbacks_;
};

} // namespace st
