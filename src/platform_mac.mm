#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#include "platform.hpp" // Includes glfw3.h, required before glfw3native.h
#define GLFW_EXPOSE_NATIVE_COCOA
#import <GLFW/glfw3native.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

@interface DesktopSnowMenuTarget : NSObject
- (void)performAction:(id)sender;
@end
@implementation DesktopSnowMenuTarget
- (void)performAction:(id)sender {
    extern void snow_dispatch_menu_action(int action);
    snow_dispatch_menu_action((int)[sender tag]);
}
@end

static void (*g_on_action)(int) = nullptr;
void snow_dispatch_menu_action(int action) { if (g_on_action) g_on_action(action); }
namespace {
NSStatusItem* g_item = nil;
DesktopSnowMenuTarget* g_target = nil;

NSMenuItem* make_item(NSMenu* menu, NSString* title, NSInteger tag, BOOL checked = NO) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
        action:@selector(performAction:) keyEquivalent:@""];
    item.tag = tag;
    item.target = g_target;
    item.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    [menu addItem:item];
    return item;
}

bool contains_screen(snow::Rect screen, snow::Rect rect) {
    return rect.right() > screen.x && rect.x < screen.right() &&
           rect.bottom() > screen.y && rect.y < screen.bottom();
}
} // namespace

namespace platform {

void prepare_process() {}

void configure_overlay(GLFWwindow* window) {
    NSWindow* native = glfwGetCocoaWindow(window);
    if (!native) return;
    [native setIgnoresMouseEvents:YES];
    [native setAcceptsMouseMovedEvents:NO];
    [native setOpaque:NO];
    [native setBackgroundColor:[NSColor clearColor]];
    [native setHasShadow:NO];
    [native setLevel:NSStatusWindowLevel];
    [native setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                  NSWindowCollectionBehaviorFullScreenAuxiliary |
                                  NSWindowCollectionBehaviorStationary];
}

SceneSnapshot query_scene(const std::vector<snow::Rect>& monitors) {
    SceneSnapshot result;
    result.fullscreen.assign(monitors.size(), false);
    const pid_t own_pid = [[NSProcessInfo processInfo] processIdentifier];
    CFArrayRef list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
                                                 kCGWindowListExcludeDesktopElements,
                                                 kCGNullWindowID);
    if (!list) return result;
    const CFIndex count = CFArrayGetCount(list);
    std::vector<bool> front_seen(monitors.size(), false);
    for (CFIndex i = 0; i < count; ++i) {
        CFDictionaryRef info = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list, i));
        if (!info) continue;
        int pid = -1, layer = 0;
        auto owner = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowOwnerPID));
        auto layer_number = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowLayer));
        if (owner) CFNumberGetValue(owner, kCFNumberIntType, &pid);
        if (layer_number) CFNumberGetValue(layer_number, kCFNumberIntType, &layer);
        if (pid == own_pid || layer < 0) continue;
        auto bounds_dict = static_cast<CFDictionaryRef>(CFDictionaryGetValue(info, kCGWindowBounds));
        CGRect cg_bounds = CGRectZero;
        if (!bounds_dict || !CGRectMakeWithDictionaryRepresentation(bounds_dict, &cg_bounds)) continue;
        const snow::Rect bounds{
            static_cast<float>(cg_bounds.origin.x), static_cast<float>(cg_bounds.origin.y),
            static_cast<float>(cg_bounds.size.width), static_cast<float>(cg_bounds.size.height)};
        if (bounds.w < 60 || bounds.h < 20) continue;
        bool intersects = false;
        bool too_large = false;
        for (std::size_t mi = 0; mi < monitors.size(); ++mi) {
            const auto& m = monitors[mi];
            if (!contains_screen(m, bounds)) continue;
            intersects = true;
            if (layer == 0 && !front_seen[mi]) {
                front_seen[mi] = true;
                if (snow::World::is_borderless_fullscreen(m, bounds, false)) {
                    result.fullscreen[mi] = true;
                    too_large = true;
                }
            }
            if (bounds.w >= m.w * 0.985f && bounds.h >= m.h * 0.985f &&
                bounds.x <= m.x + 5 && bounds.y <= m.y + 5) too_large = true;
        }
        if (!intersects || too_large) continue;
        auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(info, kCGWindowNumber));
        int id = 0;
        if (!number || !CFNumberGetValue(number, kCFNumberIntType, &id)) continue;
        bool dock = false;
        auto owner_name = static_cast<CFStringRef>(CFDictionaryGetValue(info, kCGWindowOwnerName));
        if (owner_name && CFStringCompare(owner_name, CFSTR("Dock"), 0) == kCFCompareEqualTo) dock = true;
        // Window numbers are stable while a window is alive, including when moved.
        result.surfaces.push_back({static_cast<std::uint64_t>(static_cast<unsigned>(id)),
                                   bounds, dock});
    }
    CFRelease(list);
    return result;
}

snow::Cursor cursor() {
    // NSEvent.mouseLocation is a global point in Cocoa bottom-left coordinates.
    // Convert to the top-left convention shared by CGWindowList and GLFW.
    NSPoint p = [NSEvent mouseLocation];
    NSArray<NSScreen*>* displays = [NSScreen screens];
    if ([displays count] == 0) return {};
    const CGFloat primary_top = NSMaxY([displays objectAtIndex:0].frame);
    const bool down = (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) &
                       kCGEventFlagMaskShift) != 0;
    return {static_cast<float>(p.x), static_cast<float>(primary_top - p.y), true, down};
}

bool low_power_mode() {
    NSProcessInfo* info = [NSProcessInfo processInfo];
    return [info isLowPowerModeEnabled] || info.thermalState >= NSProcessInfoThermalStateSerious;
}

std::filesystem::path settings_path() {
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") /
           "Library" / "Application Support" / "DesktopSnow" / "settings.ini";
}

void create_status_icon(void (*on_action)(int)) {
    g_on_action = on_action;
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    g_target = [[DesktopSnowMenuTarget alloc] init];
    g_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    g_item.button.title = @"❄";
    g_item.button.toolTip = @"Desktop Snow";
}

void update_status_icon(const snow::Settings& s) {
    if (!g_item) return;
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Desktop Snow"];
    make_item(menu, s.paused ? @"Resume snow" : @"Pause snow", s.paused ? Resume : Pause);
    make_item(menu, @"Clear all snow", Clear);
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* density = [[NSMenuItem alloc] initWithTitle:@"Snowfall" action:nil keyEquivalent:@""];
    NSMenu* densities = [[NSMenu alloc] initWithTitle:@"Snowfall"];
    make_item(densities, @"Light", Light, s.intensity == snow::Intensity::Light);
    make_item(densities, @"Normal", Normal, s.intensity == snow::Intensity::Normal);
    make_item(densities, @"Blizzard", Blizzard, s.intensity == snow::Intensity::Blizzard);
    density.submenu = densities;
    [menu addItem:density];
    NSMenuItem* wind = [[NSMenuItem alloc] initWithTitle:@"Wind" action:nil keyEquivalent:@""];
    NSMenu* winds = [[NSMenu alloc] initWithTitle:@"Wind"];
    make_item(winds, @"Left", WindLeft, s.wind < -1);
    make_item(winds, @"Calm", WindCalm, std::abs(s.wind) <= 1);
    make_item(winds, @"Right", WindRight, s.wind > 1);
    wind.submenu = winds;
    [menu addItem:wind];
    NSMenuItem* tool = [[NSMenuItem alloc] initWithTitle:@"Cursor tool" action:nil keyEquivalent:@""];
    NSMenu* tools = [[NSMenu alloc] initWithTitle:@"Cursor tool"];
    make_item(tools, @"Brush (move cursor)", Brush, s.tool == snow::Tool::Brush);
    make_item(tools, @"Snowballs (hold Shift)", Roll, s.tool == snow::Tool::Roll);
    make_item(tools, @"Off", ToolOff, s.tool == snow::Tool::None);
    tool.submenu = tools;
    [menu addItem:tool];
    NSMenuItem* fps = [[NSMenuItem alloc] initWithTitle:@"Frame rate" action:nil keyEquivalent:@""];
    NSMenu* fps_menu = [[NSMenu alloc] initWithTitle:@"Frame rate"];
    make_item(fps_menu, @"Eco (15 fps)", Fps15, s.target_fps == 15);
    make_item(fps_menu, @"Normal (30 fps)", Fps30, s.target_fps == 30);
    make_item(fps_menu, @"Smooth (60 fps)", Fps60, s.target_fps == 60);
    fps.submenu = fps_menu;
    [menu addItem:fps];
    make_item(menu, @"Pause in fullscreen", FullscreenToggle, s.pause_fullscreen);
    [menu addItem:[NSMenuItem separatorItem]];
    make_item(menu, @"Quit", Quit);
    g_item.menu = menu;
}

void destroy_status_icon() {
    if (g_item) [[NSStatusBar systemStatusBar] removeStatusItem:g_item];
    g_item = nil;
    g_target = nil;
    g_on_action = nullptr;
}
} // namespace platform
