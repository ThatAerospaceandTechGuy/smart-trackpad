#include "AppMac.h"

#import <Cocoa/Cocoa.h>
#import <IOKit/hidsystem/ev_keymap.h>

#include "AudioMac.h"
#include "BrightnessMac.h"
#include "Config.h"
#include "GestureEngine.h"
#include "InputMultitouch.h"
#include "LoginItemMac.h"
#include "Log.h"
#include "OverlayMac.h"
#include "Platform.h"
#include "Text.h"

#include <chrono>
#include <cmath>

namespace st {
namespace {

// Posts a media key the way the hardware keys do.  The accessibility permission
// is not required for CGEventPost to the HID tap on current macOS releases, but
// the log reports when the event could not be created so a silent no-op is
// diagnosable.
void PostMediaKey(int key) {
    for (const bool down : {true, false}) {
        const int flags = down ? 0xA : 0xB;
        NSEvent* event = [NSEvent otherEventWithType:NSEventTypeSystemDefined
                                           subtype:8
                                             data1:(key << 16) | (flags << 8)
                                             data2:-1];
        CGEventRef cgEvent = [event CGEvent];
        if (cgEvent != nullptr) {
            CGEventPost(kCGHIDEventTap, cgEvent);
        }
    }
}

void PostMediaAction(EdgeAction action) {
    switch (action) {
    case EdgeAction::MediaPlayPause:
        PostMediaKey(NX_KEYTYPE_PLAY);
        break;
    case EdgeAction::MediaNext:
        PostMediaKey(NX_KEYTYPE_NEXT);
        break;
    case EdgeAction::MediaPrevious:
        PostMediaKey(NX_KEYTYPE_PREVIOUS);
        break;
    case EdgeAction::ToggleMute:
        break;
    }
}

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

struct MacApp::Impl {
    std::unique_ptr<MultitouchInput> input;
    std::unique_ptr<CoreAudioVolume> volume;
    std::unique_ptr<MacBrightness> brightness;
    std::unique_ptr<OverlayMac> overlay;
    std::unique_ptr<GestureEngine> gestures;
    ConfigWatcher watcher;
    bool restoringSettings = false;
};

} // namespace st

// ---------------------------------------------------------------------------
// Menu bar shell
// ---------------------------------------------------------------------------

static SmartTrackpadDelegate* g_delegate = nil;

@interface SmartTrackpadDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) st::MacApp* app;
@property(nonatomic, strong) NSStatusItem* statusItem;
@property(nonatomic, strong) NSMenuItem* enabledItem;
@property(nonatomic, strong) NSMenuItem* loginItem;
@end

@implementation SmartTrackpadDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    self.statusItem = [[NSStatusBar systemStatusBar]
        statusItemWithLength:NSVariableStatusItemLength];

    // A text glyph avoids shipping an asset and scales with the menu bar.
    NSButton* button = self.statusItem.button;
    button.title = @"◧";
    button.toolTip = @"Smart Trackpad";

    NSMenu* menu = [[NSMenu alloc] init];

    self.enabledItem = [[NSMenuItem alloc] initWithTitle:@"Edge gestures"
                                                  action:@selector(toggleEnabled:)
                                           keyEquivalent:@""];
    self.enabledItem.target = self;
    self.enabledItem.state = NSControlStateValueOn;
    [menu addItem:self.enabledItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* reload = [[NSMenuItem alloc] initWithTitle:@"Reload configuration"
                                                    action:@selector(reloadConfig:)
                                             keyEquivalent:@""];
    reload.target = self;
    [menu addItem:reload];

    NSMenuItem* openConfig = [[NSMenuItem alloc] initWithTitle:@"Edit configuration…"
                                                        action:@selector(openConfig:)
                                                 keyEquivalent:@""];
    openConfig.target = self;
    [menu addItem:openConfig];

    NSMenuItem* openFolder = [[NSMenuItem alloc] initWithTitle:@"Open data folder"
                                                        action:@selector(openFolder:)
                                                 keyEquivalent:@""];
    openFolder.target = self;
    [menu addItem:openFolder];

    NSMenuItem* openLog = [[NSMenuItem alloc] initWithTitle:@"Open log"
                                                     action:@selector(openLog:)
                                              keyEquivalent:@""];
    openLog.target = self;
    [menu addItem:openLog];

    [menu addItem:[NSMenuItem separatorItem]];

    self.loginItem = [[NSMenuItem alloc] initWithTitle:@"Start at login"
                                                action:@selector(toggleLoginItem:)
                                         keyEquivalent:@""];
    self.loginItem.target = self;
    self.loginItem.state = st::IsLoginItemEnabled() ? NSControlStateValueOn
                                                    : NSControlStateValueOff;
    [menu addItem:self.loginItem];

    [menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* about = [[NSMenuItem alloc] initWithTitle:@"About Smart Trackpad"
                                                   action:@selector(showAbout:)
                                            keyEquivalent:@""];
    about.target = self;
    [menu addItem:about];

    NSMenuItem* quit = [[NSMenuItem alloc] initWithTitle:@"Quit Smart Trackpad"
                                                  action:@selector(quit:)
                                           keyEquivalent:@"q"];
    quit.target = self;
    [menu addItem:quit];

    self.statusItem.menu = menu;
}

- (void)toggleEnabled:(id)sender {
    (void)sender;
    if (self.app == nullptr) {
        return;
    }
    self.app->SetEnabled(!self.app->Enabled());
    self.enabledItem.state =
        self.app->Enabled() ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)reloadConfig:(id)sender {
    (void)sender;
    if (self.app != nullptr) {
        self.app->ReloadConfig();
    }
}

- (void)openConfig:(id)sender {
    (void)sender;
    if (self.app != nullptr) {
        self.app->OpenConfiguration();
    }
}

- (void)openFolder:(id)sender {
    (void)sender;
    if (self.app != nullptr) {
        self.app->OpenDataFolder();
    }
}

- (void)openLog:(id)sender {
    (void)sender;
    if (self.app != nullptr) {
        self.app->OpenLog();
    }
}

- (void)toggleLoginItem:(id)sender {
    (void)sender;
    if (self.app == nullptr) {
        return;
    }
    const bool enable = !st::IsLoginItemEnabled();
    self.app->SetStartAtLogin(enable);
    self.loginItem.state = enable ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)showAbout:(id)sender {
    (void)sender;
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Smart Trackpad";
    alert.informativeText = [NSString
        stringWithFormat:@"Version %s\n\nLeft edge: volume\nTop edge: media\n"
                         @"Right edge: brightness (off by default)\n\n"
                         @"Configuration: %s",
                         SMARTTRACKPAD_VERSION_TEXT,
                         st::text::ToUtf8(st::GetConfigPath().wstring()).c_str()];
    [alert runModal];
}

- (void)quit:(id)sender {
    (void)sender;
    [NSApp terminate:nil];
}

@end

namespace st {

MacApp::MacApp() : impl_(std::make_unique<Impl>()) {}

MacApp::~MacApp() {
    if (impl_) {
        impl_->watcher.Stop();
        if (impl_->input) {
            impl_->input->Stop();
        }
    }
}

bool MacApp::Initialize() {
    Impl& self = *impl_;
    config_ = LoadConfig(GetConfigPath());
    self.watcher.Watch(config_.configPath, [this] { ReloadConfig(); });

    self.overlay = std::make_unique<OverlayMac>();
    self.overlay->SetShowPercentage(config_.osdShowPercentage);

    self.volume = std::make_unique<CoreAudioVolume>([this](const LevelFeedback& level) {
        if (impl_->overlay) {
            impl_->overlay->Show(OsdKind::Volume, level.percent, level.muted);
        }
    });
    self.volume->Initialize();

    self.brightness = std::make_unique<MacBrightness>([this](const LevelFeedback& level) {
        if (impl_->overlay) {
            impl_->overlay->Show(OsdKind::Brightness, level.percent, false);
        }
    });
    self.brightness->Initialize();
    self.brightness->SetTargets(config_.brightnessTargets);

    self.gestures = std::make_unique<GestureEngine>(
        config_.gestures,
        [this](int percent) {
            if (!enabled_) {
                return;
            }
            if (impl_->overlay) {
                impl_->overlay->Show(OsdKind::Volume, percent, false);
            }
            if (impl_->volume) {
                impl_->volume->SetTargetPercent(percent);
            }
        },
        [this](EdgeAction action) {
            if (!enabled_) {
                return;
            }
            switch (action) {
            case EdgeAction::ToggleMute:
                if (impl_->volume) {
                    impl_->volume->ToggleMute();
                }
                break;
            case EdgeAction::MediaPlayPause:
            case EdgeAction::MediaNext:
            case EdgeAction::MediaPrevious:
                PostMediaAction(action);
                break;
            }
        },
        [this](int percent) {
            if (!enabled_) {
                return;
            }
            if (impl_->overlay) {
                impl_->overlay->Show(OsdKind::Brightness, percent, false);
            }
            if (impl_->brightness) {
                impl_->brightness->SetTargetPercent(percent);
            }
        },
        [] { return NowMs(); });

    self.input = std::make_unique<MultitouchInput>(MultitouchInput::Callbacks{
        [this](float x, float y) {
            if (impl_->gestures) {
                impl_->gestures->OnContactDown(x, y);
            }
            if (contactLogging_) {
                LogFormat(LogLevel::Info, L"contact down x=%.3f y=%.3f",
                          static_cast<double>(x), static_cast<double>(y));
            }
        },
        [this](float x, float y) {
            if (impl_->gestures) {
                impl_->gestures->OnContactMove(x, y);
            }
            if (contactLogging_) {
                LogFormat(LogLevel::Info, L"contact move x=%.3f y=%.3f zone=%d",
                          static_cast<double>(x), static_cast<double>(y),
                          impl_->gestures
                              ? static_cast<int>(impl_->gestures->ActiveZone())
                              : 0);
            }
        },
        [this] {
            if (impl_->gestures) {
                impl_->gestures->OnContactUp();
            }
            if (contactLogging_) {
                LogMessage(LogLevel::Info, L"contact up");
            }
        },
        [this] {
            if (impl_->gestures) {
                impl_->gestures->OnContactCancel();
            }
        }});

    if (!self.input->Start()) {
        LogMessage(LogLevel::Warning,
                   L"Running without input: gestures are unavailable.");
    }

    LogFormat(LogLevel::Info, L"Input: %ls",
              self.input->DescribeDevices().c_str());
    LogFormat(LogLevel::Info,
              L"Zones: left %ls %.0f%% (absolute volume), right %ls %.0f%% "
              L"(absolute brightness), top %ls %.0f%%; activation %.1f%%, media "
              L"step %.1f%%.",
              config_.gestures.leftEnabled ? L"on" : L"off",
              config_.gestures.leftBand * 100.0f,
              config_.gestures.rightEnabled ? L"on" : L"off",
              config_.gestures.rightBand * 100.0f,
              config_.gestures.topEnabled ? L"on" : L"off",
              config_.gestures.topBand * 100.0f,
              config_.gestures.activationDistance * 100.0f,
              config_.gestures.stepDistance * 100.0f);
    return true;
}

int MacApp::Run() {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];

        SmartTrackpadDelegate* delegate = [[SmartTrackpadDelegate alloc] init];
        delegate.app = this;
        g_delegate = delegate; // NSApplication.delegate is weak.
        application.delegate = delegate;
        [application run];
    }
    return 0;
}

void MacApp::ReloadConfig() {
    config_ = LoadConfig(GetConfigPath());
    if (impl_->gestures) {
        impl_->gestures->SetSettings(config_.gestures);
    }
    if (impl_->brightness) {
        impl_->brightness->SetTargets(config_.brightnessTargets);
    }
    if (impl_->overlay) {
        impl_->overlay->SetShowPercentage(config_.osdShowPercentage);
    }
    LogMessage(LogLevel::Info, L"Configuration reloaded.");
}

void MacApp::SetEnabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled && impl_->gestures) {
        impl_->gestures->Reset();
    }
    LogFormat(LogLevel::Info, L"Edge gestures %ls.", enabled ? L"enabled" : L"disabled");
}

void MacApp::SetStartAtLogin(bool enabled) {
    SetLoginItem(enabled);
    config_.startWithWindows = enabled;
    SaveConfig(config_.configPath, config_);
}

void MacApp::OpenConfiguration() {
    NSString* path = [NSString stringWithUTF8String:config_.configPath.string().c_str()];
    [[NSWorkspace sharedWorkspace] openFile:path];
}

void MacApp::OpenDataFolder() {
    NSString* path =
        [NSString stringWithUTF8String:GetDataDirectory().string().c_str()];
    [[NSWorkspace sharedWorkspace] openFile:path];
}

void MacApp::OpenLog() {
    NSString* path = [NSString stringWithUTF8String:LogPath().string().c_str()];
    [[NSWorkspace sharedWorkspace] openFile:path];
}

} // namespace st
