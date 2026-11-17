#pragma once

#include <windows.h>

#include <string>

#include "GestureEngine.h"

namespace st {

// Everything the zone-test window draws.  The application fills this in from
// the live input and action callbacks; the window itself has no state beyond
// the last snapshot.
struct DiagnosticsSnapshot {
    bool hasContact = false;
    float x = 0.0f;
    float y = 0.0f;
    EdgeZone zone = EdgeZone::None;
    std::wstring deviceSummary;
    std::wstring lastAction;
    int volumePercent = -1;
    int brightnessPercent = -1;
    int volumeUpdates = 0;
    int brightnessUpdates = 0;
    int mediaSteps = 0;
    int taps = 0;
    GestureSettings settings;
};

// "Zone test" window: draws the pad surface, the configured bands and the live
// contact so a user can see whether their touchpad reports absolute positions
// and where the zones actually are.
class DiagnosticsWindow {
public:
    DiagnosticsWindow();
    ~DiagnosticsWindow();

    DiagnosticsWindow(const DiagnosticsWindow&) = delete;
    DiagnosticsWindow& operator=(const DiagnosticsWindow&) = delete;

    bool Create(HINSTANCE instance);
    void Show();
    void Hide();
    [[nodiscard]] bool IsVisible() const;
    void Update(const DiagnosticsSnapshot& snapshot);

private:
    static LRESULT CALLBACK WindowProcStatic(HWND hwnd, UINT message, WPARAM wParam,
                                             LPARAM lParam);
    LRESULT WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint(HDC target, const RECT& client);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    DiagnosticsSnapshot snapshot_;
};

} // namespace st
