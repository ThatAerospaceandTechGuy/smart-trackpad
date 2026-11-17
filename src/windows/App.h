#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "Config.h"
#include "Diagnostics.h"
#include "GestureEngine.h"
#include "WindowNames.h"

namespace st {

enum class StartupAction {
    None,
    Settings,
    ZoneTest,
};

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

    // Optional action performed once the message loop is running, used by the
    // command-line switches and by flags forwarded from a second instance.
    void SetStartupAction(StartupAction action);

private:
    static LRESULT CALLBACK InputWndProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                               LPARAM lParam);
    LRESULT InputWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateInputWindow();
    void CreateTrayIcon();

    void OnToggleEnabled();
    void OnToggleBrightness();
    void OnToggleStartWithWindows();
    void OnOpenSettings();
    void OnOpenZoneTest();
    void OnUninstall();
    void OnAbout();
    void OnCopyLogPath();
    void OnReloadConfig();
    void OpenPath(const std::filesystem::path& path) const;
    void LogStartupSummary() const;

    // Applies a freshly loaded configuration to the live components.
    void ApplySettings();
    void UpdateDiagnostics();
    void SetSuspended(bool suspended);

    HINSTANCE instance_ = nullptr;
    HWND inputWindow_ = nullptr;
    AppConfig config_;
    std::unique_ptr<ActionDispatcher> actions_;
    std::unique_ptr<GestureEngine> gestures_;
    std::unique_ptr<RawInputSource> rawInput_;
    std::unique_ptr<TrayIcon> tray_;
    std::unique_ptr<DiagnosticsWindow> diagnostics_;
    ConfigWatcher configWatcher_;

    bool enabled_ = true;
    bool suspended_ = false;
    StartupAction startupAction_ = StartupAction::None;
    UINT commandMessage_ = 0;

    // Live state for the zone-test window.
    bool hasContact_ = false;
    float contactX_ = 0.0f;
    float contactY_ = 0.0f;
    std::wstring deviceSummary_;
    std::wstring lastAction_;
    int volumePercent_ = -1;
    int brightnessPercent_ = -1;
    int volumeUpdates_ = 0;
    int brightnessUpdates_ = 0;
    int mediaSteps_ = 0;
    int tapCount_ = 0;
};

} // namespace st
