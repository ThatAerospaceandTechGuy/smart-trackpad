#include "OverlayMac.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr CGFloat kWidth = 240.0;
constexpr CGFloat kHeight = 56.0;
constexpr CGFloat kMarginFromBottom = 96.0;
constexpr NSTimeInterval kVisibleSeconds = 2.0;
} // namespace

// The HUD draws itself: a rounded dark card, a level bar and the value.  Kept in
// one small view so the overlay has no images or assets.
@interface SmartTrackpadHudView : NSView
@property(nonatomic) CGFloat level;
@property(nonatomic) BOOL muted;
@property(nonatomic) BOOL showPercentage;
@property(nonatomic, copy) NSString* label;
@end

@implementation SmartTrackpadHudView

- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;

    const NSRect bounds = self.bounds;
    NSBezierPath* card =
        [NSBezierPath bezierPathWithRoundedRect:bounds xRadius:10 yRadius:10];
    [[NSColor colorWithCalibratedWhite:0.12 alpha:0.92] setFill];
    [card fill];

    const CGFloat inset = 16.0;
    const CGFloat barHeight = 5.0;
    const NSRect track = NSMakeRect(inset, NSHeight(bounds) - inset - barHeight,
                                    NSWidth(bounds) - inset * 2, barHeight);
    NSBezierPath* trackPath =
        [NSBezierPath bezierPathWithRoundedRect:track xRadius:2.5 yRadius:2.5];
    [[NSColor colorWithCalibratedWhite:0.35 alpha:1.0] setFill];
    [trackPath fill];

    const CGFloat filledWidth = std::max(1.0, NSWidth(track) * self.level);
    NSRect filled = track;
    filled.size.width = filledWidth;
    NSBezierPath* fillPath =
        [NSBezierPath bezierPathWithRoundedRect:filled xRadius:2.5 yRadius:2.5];
    const NSColor* fillColour =
        self.muted ? [NSColor colorWithCalibratedWhite:0.55 alpha:1.0]
                   : [NSColor colorWithCalibratedWhite:0.85 alpha:1.0];
    [fillColour setFill];
    [fillPath fill];

    if (self.showPercentage && self.label.length > 0) {
        NSDictionary* attributes = @{
            NSFontAttributeName : [NSFont systemFontOfSize:15 weight:NSFontWeightMedium],
            NSForegroundColorAttributeName : [NSColor whiteColor],
        };
        [self.label drawAtPoint:NSMakePoint(inset, inset - 4) withAttributes:attributes];
    }
}

@end

namespace st {

OverlayMac::OverlayMac() {
    NSWindow* panel = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, kWidth, kHeight)
                  styleMask:NSWindowStyleMaskBorderless
                    backing:NSBackingStoreBuffered
                      defer:NO];
    panel.opaque = NO;
    panel.backgroundColor = [NSColor clearColor];
    panel.hasShadow = YES;
    panel.level = NSFloatingWindowLevel;
    panel.ignoresMouseEvents = YES;
    panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                               NSWindowCollectionBehaviorStationary |
                               NSWindowCollectionBehaviorIgnoresCycle;

    SmartTrackpadHudView* view =
        [[SmartTrackpadHudView alloc] initWithFrame:NSMakeRect(0, 0, kWidth, kHeight)];
    view.showPercentage = YES;
    panel.contentView = view;

    panel_ = (__bridge_retained void*)panel;
    view_ = (__bridge_retained void*)view;
}

OverlayMac::~OverlayMac() {
    if (panel_ != nullptr) {
        NSWindow* panel = (__bridge_transfer NSWindow*)panel_;
        [panel orderOut:nil];
        panel_ = nullptr;
    }
    if (view_ != nullptr) {
        (void)(__bridge_transfer SmartTrackpadHudView*)view_;
        view_ = nullptr;
    }
}

void OverlayMac::SetShowPercentage(bool show) {
    if (view_ == nullptr) {
        return;
    }
    SmartTrackpadHudView* view = (__bridge SmartTrackpadHudView*)view_;
    dispatch_async(dispatch_get_main_queue(), ^{
      view.showPercentage = show;
      [view setNeedsDisplay:YES];
    });
}

void OverlayMac::Show(OsdKind kind, int percent, bool muted) {
    if (panel_ == nullptr || view_ == nullptr) {
        return;
    }

    const CGFloat level = std::clamp(percent / 100.0, 0.0, 1.0);
    NSString* label =
        kind == OsdKind::Brightness
            ? [NSString stringWithFormat:@"☀ %d%%", percent]
            : (muted ? [NSString stringWithFormat:@"🔇 %d%%", percent]
                     : [NSString stringWithFormat:@"🔊 %d%%", percent]);

    NSWindow* panel = (__bridge NSWindow*)panel_;
    SmartTrackpadHudView* view = (__bridge SmartTrackpadHudView*)view_;

    dispatch_async(dispatch_get_main_queue(), ^{
      view.level = level;
      view.muted = muted;
      view.label = label;
      [view setNeedsDisplay:YES];

      NSScreen* screen = [NSScreen mainScreen];
      if (screen != nil) {
          const NSRect visible = screen.visibleFrame;
          const CGFloat x = NSMidX(visible) - kWidth / 2.0;
          const CGFloat y = NSMinY(visible) + kMarginFromBottom;
          [panel setFrame:NSMakeRect(x, y, kWidth, kHeight) display:YES];
      }

      [panel orderFrontRegardless];

      // Restart the hide timer on every update so a continuous drag keeps the
      // HUD on screen.
      [NSObject cancelPreviousPerformRequestsWithTarget:panel
                                               selector:@selector(orderOut:)
                                                 object:nil];
      [panel performSelector:@selector(orderOut:) withObject:nil afterDelay:kVisibleSeconds];
    });
}

} // namespace st
