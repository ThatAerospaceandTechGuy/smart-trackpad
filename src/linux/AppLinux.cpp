#include "AppLinux.h"

#include "AudioLinux.h"
#include "BrightnessLinux.h"
#include "ContactTracker.h"
#include "GestureEngine.h"
#include "InputEvdev.h"
#include "Log.h"
#include "OsdLinux.h"
#include "Platform.h"
#include "Text.h"

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <fcntl.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace st {
namespace {

int g_signalPipe[2] = {-1, -1};

void HandleSignal(int) {
    const char byte = 1;
    (void)!write(g_signalPipe[1], &byte, 1);
}

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

struct LinuxApp::Impl {
    AppConfig config;
    ConfigWatcher watcher;

    std::unique_ptr<EvdevInput> input;
    std::unique_ptr<LinuxVolumeController> volume;
    std::unique_ptr<LinuxBrightnessController> brightness;
    std::unique_ptr<X11Overlay> overlay;
    std::unique_ptr<GestureEngine> gestures;

    std::atomic<bool> enabled{true};
    std::string deviceSummary;

    void BuildEngine();
    void ApplyConfig();
    void Reload();
};

void LinuxApp::Impl::BuildEngine() {
    gestures = std::make_unique<GestureEngine>(
        config.gestures,
        [this](int percent) {
            if (!enabled) {
                return;
            }
            if (overlay) {
                overlay->Show(OsdKind::Volume, percent);
            }
            if (volume) {
                volume->SetTargetPercent(percent);
            }
        },
        [this](EdgeAction action) {
            if (!enabled) {
                return;
            }
            switch (action) {
            case EdgeAction::MediaNext:
            case EdgeAction::MediaPrevious:
            case EdgeAction::MediaPlayPause:
                if (volume) {
                    // Media keys need an input device injection; on Linux the
                    // portable way is playerctl, which speaks the MPRIS D-Bus
                    // interface every media player implements.
                    const char* command =
                        action == EdgeAction::MediaNext
                            ? "playerctl next 2>/dev/null"
                            : (action == EdgeAction::MediaPrevious
                                   ? "playerctl previous 2>/dev/null"
                                   : "playerctl play-pause 2>/dev/null");
                    (void)RunCommand(command);
                }
                break;
            case EdgeAction::ToggleMute:
                if (volume) {
                    volume->ToggleMute();
                }
                break;
            }
        },
        [this](int percent) {
            if (!enabled) {
                return;
            }
            if (overlay) {
                overlay->Show(OsdKind::Brightness, percent);
            }
            if (brightness) {
                brightness->SetTargetPercent(percent);
            }
        },
        [] { return NowMs(); });
}

void LinuxApp::Impl::ApplyConfig() {
    if (gestures) {
        gestures->SetSettings(config.gestures);
    }
    if (brightness) {
        brightness->SetTargets(config.brightnessTargets);
    }
    if (overlay) {
        overlay->SetShowPercentage(config.osdShowPercentage);
    }
}

void LinuxApp::Impl::Reload() {
    config = LoadConfig(GetConfigPath());
    ApplyConfig();
    LogMessage(LogLevel::Info, L"Configuration reloaded.");
}

LinuxApp::LinuxApp() : impl_(std::make_unique<Impl>()) {}

LinuxApp::~LinuxApp() {
    if (impl_) {
        impl_->watcher.Stop();
        if (impl_->input) {
            impl_->input->Stop();
        }
    }
}

bool LinuxApp::Initialize() {
    Impl& self = *impl_;
    self.config = LoadConfig(GetConfigPath());
    self.watcher.Watch(self.config.configPath, [this] { ReloadConfig(); });

    self.overlay = std::make_unique<X11Overlay>();
    self.overlay->SetShowPercentage(self.config.osdShowPercentage);

    self.volume = std::make_unique<LinuxVolumeController>([this](const LevelFeedback&) {
        // The overlay already shows the requested level; a read-back update
        // would need the kind, which the batch API does not carry.
    });

    self.brightness = std::make_unique<LinuxBrightnessController>(
        [this](const LevelFeedback&) {});
    self.brightness->SetTargets(self.config.brightnessTargets);

    self.BuildEngine();

    self.input = std::make_unique<EvdevInput>(EvdevInput::Callbacks{
        [this](float x, float y) {
            if (impl_->gestures) {
                impl_->gestures->OnContactDown(x, y);
            }
        },
        [this](float x, float y) {
            if (impl_->gestures) {
                impl_->gestures->OnContactMove(x, y);
            }
        },
        [this] {
            if (impl_->gestures) {
                impl_->gestures->OnContactUp();
            }
        },
        [this] {
            if (impl_->gestures) {
                impl_->gestures->OnContactCancel();
            }
        }});

    if (!self.input->Start(self.config.devices)) {
        return false;
    }

    if (pipe2(g_signalPipe, O_NONBLOCK | O_CLOEXEC) != 0) {
        LogMessage(LogLevel::Error, L"Could not create the signal pipe.");
        return false;
    }
    struct sigaction action {};
    action.sa_handler = &HandleSignal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    LogFormat(LogLevel::Info, L"Input: %ls", self.input->DescribeDevices().c_str());
    LogFormat(LogLevel::Info,
              L"Zones: left %ls %.0f%% (absolute volume), right %ls %.0f%% "
              L"(absolute brightness), top %ls %.0f%%; activation %.1f%%, media "
              L"step %.1f%%.",
              self.config.gestures.leftEnabled ? L"on" : L"off",
              self.config.gestures.leftBand * 100.0f,
              self.config.gestures.rightEnabled ? L"on" : L"off",
              self.config.gestures.rightBand * 100.0f,
              self.config.gestures.topEnabled ? L"on" : L"off",
              self.config.gestures.topBand * 100.0f,
              self.config.gestures.activationDistance * 100.0f,
              self.config.gestures.stepDistance * 100.0f);
    return true;
}

int LinuxApp::Run() {
    Impl& self = *impl_;

    while (true) {
        std::vector<pollfd> descriptors;
        const std::vector<int> devices = self.input->FileDescriptors();
        descriptors.reserve(devices.size() + 1);
        for (const int fd : devices) {
            descriptors.push_back(pollfd{fd, POLLIN, 0});
        }
        descriptors.push_back(pollfd{g_signalPipe[0], POLLIN, 0});

        const int ready = poll(descriptors.data(), descriptors.size(), 500);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if ((descriptors.back().revents & POLLIN) != 0) {
            char buffer[16];
            while (read(g_signalPipe[0], buffer, sizeof(buffer)) > 0) {
            }
            LogMessage(LogLevel::Info, L"Signal received; exiting.");
            break;
        }

        if (ready > 0) {
            self.input->HandleReadable();
        }
    }

    self.watcher.Stop();
    self.input->Stop();
    return 0;
}

void LinuxApp::ReloadConfig() {
    impl_->Reload();
}

void LinuxApp::ApplyStartWithWindows(bool enabled) {
    // On Linux "start with Windows" means an XDG autostart entry.
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return;
    }

    const std::filesystem::path entry =
        std::filesystem::path(home) / ".config" / "autostart" / "smarttrackpad.desktop";

    std::error_code error;
    if (!enabled) {
        std::filesystem::remove(entry, error);
        LogMessage(LogLevel::Info, L"Autostart entry removed.");
        return;
    }

    std::filesystem::create_directories(entry.parent_path(), error);
    std::ofstream file(entry, std::ios::trunc);
    if (!file) {
        LogMessage(LogLevel::Warning, L"Could not write the autostart entry.");
        return;
    }

    file << "[Desktop Entry]\n"
         << "Type=Application\n"
         << "Name=Smart Trackpad\n"
         << "Comment=Edge gestures for the touchpad\n"
         << "Exec=" << ExecutablePath().string() << "\n"
         << "X-GNOME-Autostart-enabled=true\n"
         << "Terminal=false\n";
    LogFormat(LogLevel::Info, L"Autostart entry written to \"%ls\".", st::text::PathToWide(entry).c_str());
}

} // namespace st
