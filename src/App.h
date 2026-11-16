#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>

#include "Config.h"
#include "GestureEngine.h"

namespace st {

class ActionDispatcher;
class RawInputSource;
class TrayIcon;

class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Initialize(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK InputWndProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam);
    LRESULT InputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateInputWindow();
    void CreateTrayIcon();
    void OnToggleEnabled();
    void OpenPath(const std::filesystem::path& path) const;
    void LogStartupSummary() const;

    HINSTANCE instance_ = nullptr;
    HWND inputWindow_ = nullptr;
    AppConfig config_;
    std::unique_ptr<ActionDispatcher> actions_;
    std::unique_ptr<GestureEngine> gestures_;
    std::unique_ptr<RawInputSource> rawInput_;
    std::unique_ptr<TrayIcon> tray_;
    bool enabled_ = true;
};

} // namespace st
