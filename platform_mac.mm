#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <mach-o/dyld.h>
#include "platform_mac.h"
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <chrono>

// ==================== 全局热键 ====================
static EventHandlerRef g_hotkeyHandlerRef = NULL;
static std::map<int, EventHotKeyRef> g_hotkeyRefs;
static std::queue<int> g_hotkeyQueue;
static std::mutex g_hotkeyMutex;

static OSStatus hotkeyCallback(EventHandlerCallRef, EventRef event, void*) {
    EventHotKeyID hkId;
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                      NULL, sizeof(hkId), NULL, &hkId);
    {
        std::lock_guard<std::mutex> lk(g_hotkeyMutex);
        g_hotkeyQueue.push((int)hkId.id);
    }
    return noErr;
}

int platform_pop_hotkey() {
    std::lock_guard<std::mutex> lk(g_hotkeyMutex);
    if (g_hotkeyQueue.empty()) return -1;
    int v = g_hotkeyQueue.front();
    g_hotkeyQueue.pop();
    return v;
}

struct KeyEntry { const char* name; UInt32 code; };
static const KeyEntry KEYS[] = {
    {"a", kVK_ANSI_A}, {"b", kVK_ANSI_B}, {"c", kVK_ANSI_C}, {"d", kVK_ANSI_D},
    {"e", kVK_ANSI_E}, {"f", kVK_ANSI_F}, {"g", kVK_ANSI_G}, {"h", kVK_ANSI_H},
    {"i", kVK_ANSI_I}, {"j", kVK_ANSI_J}, {"k", kVK_ANSI_K}, {"l", kVK_ANSI_L},
    {"m", kVK_ANSI_M}, {"n", kVK_ANSI_N}, {"o", kVK_ANSI_O}, {"p", kVK_ANSI_P},
    {"q", kVK_ANSI_Q}, {"r", kVK_ANSI_R}, {"s", kVK_ANSI_S}, {"t", kVK_ANSI_T},
    {"u", kVK_ANSI_U}, {"v", kVK_ANSI_V}, {"w", kVK_ANSI_W}, {"x", kVK_ANSI_X},
    {"y", kVK_ANSI_Y}, {"z", kVK_ANSI_Z},
    {"0", kVK_ANSI_0}, {"1", kVK_ANSI_1}, {"2", kVK_ANSI_2}, {"3", kVK_ANSI_3},
    {"4", kVK_ANSI_4}, {"5", kVK_ANSI_5}, {"6", kVK_ANSI_6}, {"7", kVK_ANSI_7},
    {"8", kVK_ANSI_8}, {"9", kVK_ANSI_9},
    {"space", kVK_Space}, {"enter", kVK_Return}, {"return", kVK_Return},
    {"tab", kVK_Tab}, {"escape", kVK_Escape}, {"esc", kVK_Escape},
    {"backspace", kVK_Delete},
    {"f1", kVK_F1}, {"f2", kVK_F2}, {"f3", kVK_F3}, {"f4", kVK_F4},
    {"f5", kVK_F5}, {"f6", kVK_F6}, {"f7", kVK_F7}, {"f8", kVK_F8},
    {"f9", kVK_F9}, {"f10", kVK_F10}, {"f11", kVK_F11}, {"f12", kVK_F12},
};

static UInt32 nameToKeyCode(const std::string& name) {
    for (auto& e : KEYS) if (name == e.name) return e.code;
    return 0;
}

static int parseCombo(const std::string& combo, UInt32& outMods, UInt32& outKey) {
    outMods = 0; outKey = 0;
    std::string s = combo;
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    size_t start = 0;
    while (start < s.size()) {
        size_t pos = s.find('+', start);
        std::string tok = (pos == std::string::npos) ? s.substr(start) : s.substr(start, pos - start);
        if (tok == "cmd" || tok == "command" || tok == "super") outMods |= cmdKey;
        else if (tok == "shift")  outMods |= shiftKey;
        else if (tok == "alt" || tok == "option") outMods |= optionKey;
        else if (tok == "ctrl" || tok == "control") outMods |= controlKey;
        else outKey = nameToKeyCode(tok);
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return outKey != 0 ? 1 : 0;
}

int platform_register_hotkey(int index, const char* combo) {
    if (!combo || !*combo) return 0;
    UInt32 mods = 0, code = 0;
    if (!parseCombo(combo, mods, code)) return 0;

    EventHotKeyID hkId;
    hkId.signature = 'QPHK';
    hkId.id = (UInt32)index;

    EventHotKeyRef ref = NULL;
    OSStatus st = RegisterEventHotKey(code, mods, hkId,
                                      GetApplicationEventTarget(), 0, &ref);
    if (st != noErr) return 0;
    g_hotkeyRefs[index] = ref;
    return 1;
}

void platform_unregister_all_hotkeys() {
    for (auto& kv : g_hotkeyRefs) {
        if (kv.second) UnregisterEventHotKey(kv.second);
    }
    g_hotkeyRefs.clear();
}

// ==================== 剪贴板 ====================
void platform_set_clipboard(const char* text) {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        [pb setString:[NSString stringWithUTF8String:(text ? text : "")]
              forType:NSPasteboardTypeString];
    }
}

char* platform_get_clipboard() {
    @autoreleasepool {
        NSPasteboard* pb = [NSPasteboard generalPasteboard];
        NSString* s = [pb stringForType:NSPasteboardTypeString];
        if (!s) return NULL;
        return strdup([s UTF8String]);
    }
}

static int isModifierDown() {
    bool ctrl  = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Control);
    bool shift = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Shift);
    bool cmd   = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Command);
    bool alt   = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, kVK_Option);
    return (ctrl || shift || cmd || alt) ? 1 : 0;
}

void platform_send_paste() {
    for (int i = 0; i < 30 && isModifierDown(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    CGEventSourceRef src = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef down = CGEventCreateKeyboardEvent(src, kVK_ANSI_V, true);
    CGEventRef up   = CGEventCreateKeyboardEvent(src, kVK_ANSI_V, false);
    CGEventSetFlags(down, kCGEventFlagMaskCommand);
    CGEventSetFlags(up,   kCGEventFlagMaskCommand);
    CGEventPost(kCGHIDEventTap, down);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CGEventPost(kCGHIDEventTap, up);
    CFRelease(down);
    CFRelease(up);
    if (src) CFRelease(src);
}

// ==================== 系统托盘 ====================
static NSStatusItem* g_trayItem = nil;
static NSMenu* g_trayMenu = nil;
static void (*g_trayCb)(int) = NULL;

@interface TrayTarget : NSObject
- (void)itemClicked:(NSMenuItem*)sender;
@end

@implementation TrayTarget
- (void)itemClicked:(NSMenuItem*)sender {
    if (g_trayCb) g_trayCb((int)sender.tag);
}
@end

static TrayTarget* g_trayTarget = nil;

static void addTrayItem(NSMenu* menu, int tag, NSString* label) {
    NSMenuItem* item = [[NSMenuItem alloc]
        initWithTitle:label action:@selector(itemClicked:) keyEquivalent:@""];
    item.target = g_trayTarget;
    item.tag = tag;
    [menu addItem:item];
}

void platform_create_tray(const char* tooltip) {
    @autoreleasepool {
        if (!g_trayTarget) g_trayTarget = [[TrayTarget alloc] init];

        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        g_trayItem = [[NSStatusBar systemStatusBar]
            statusItemWithLength:NSVariableStatusItemLength];

        NSImage* image = [NSImage imageWithSize:NSMakeSize(18, 18)
                                        flipped:NO
                                 drawingHandler:^BOOL(NSRect dstRect) {
            (void)dstRect;
            [[NSColor blackColor] setFill];
            NSBezierPath* bg = [NSBezierPath bezierPathWithRoundedRect:
                NSMakeRect(1.5, 1.5, 15, 15) xRadius:3.5 yRadius:3.5];
            [bg fill];

            [[NSColor whiteColor] setStroke];
            for (int i = 0; i < 3; ++i) {
                NSBezierPath* line = [NSBezierPath bezierPath];
                [line moveToPoint:NSMakePoint(5, 12 - i * 3.5)];
                [line lineToPoint:NSMakePoint(13, 12 - i * 3.5)];
                [line setLineWidth:1.6];
                [line setLineCapStyle:NSLineCapStyleRound];
                [line stroke];
            }
            return YES;
        }];
        [image setTemplate:YES];

        g_trayItem.button.image = image;
        g_trayItem.button.toolTip =
            [NSString stringWithUTF8String:(tooltip ? tooltip : "QuickPhrase")];
        g_trayItem.button.target = g_trayTarget;

        g_trayMenu = [[NSMenu alloc] init];
        g_trayItem.menu = g_trayMenu;

        addTrayItem(g_trayMenu, 0, @"显示主窗口");
        addTrayItem(g_trayMenu, 1, @"暂停热键");
        [g_trayMenu addItem:[NSMenuItem separatorItem]];
        addTrayItem(g_trayMenu, 2, @"退出");
    }
}

void platform_tray_set_callback(void (*cb)(int)) {
    g_trayCb = cb;
}

void platform_tray_set_paused(int paused) {
    @autoreleasepool {
        if (!g_trayMenu) return;
        for (NSMenuItem* item in g_trayMenu.itemArray) {
            if (item.tag == 1) {
                item.title = paused ? @"恢复热键" : @"暂停热键";
                break;
            }
        }
    }
}

// ==================== 开机自启 ====================
static NSString* launchAgentPath() {
    return [NSHomeDirectory() stringByAppendingPathComponent:
            @"Library/LaunchAgents/com.user.quickphrase.plist"];
}

int platform_get_launch_at_login() {
    @autoreleasepool {
        return [[NSFileManager defaultManager]
                fileExistsAtPath:launchAgentPath()] ? 1 : 0;
    }
}

int platform_set_launch_at_login(int enable) {
    @autoreleasepool {
        NSString* path = launchAgentPath();
        if (!enable) {
            NSString* cmd = [NSString stringWithFormat:
                @"launchctl unload '%@' 2>/dev/null; rm -f '%@'", path, path];
            system([cmd UTF8String]);
            return 1;
        }

        char exePath[4096];
        uint32_t sz = sizeof(exePath);
        if (_NSGetExecutablePath(exePath, &sz) != 0) return 0;

        NSString* plist = [NSString stringWithFormat:
            @"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
            "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.user.quickphrase</string>\n"
            "  <key>ProgramArguments</key><array>\n"
            "    <string>%s</string>\n"
            "  </array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n", exePath];

        NSError* err = nil;
        [plist writeToFile:path atomically:YES
                  encoding:NSUTF8StringEncoding error:&err];
        if (err) return 0;

        NSString* cmd = [NSString stringWithFormat:
            @"launchctl unload '%@' 2>/dev/null; launchctl load '%@'", path, path];
        system([cmd UTF8String]);
        return 1;
    }
}

// ==================== 文件对话框 ====================
char* platform_open_file_dialog() {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URL;
            return strdup([[url path] UTF8String]);
        }
        return NULL;
    }
}

char* platform_save_file_dialog(const char* default_name) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (default_name && *default_name) {
            panel.nameFieldStringValue = [NSString stringWithUTF8String:default_name];
        }
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = panel.URL;
            return strdup([[url path] UTF8String]);
        }
        return NULL;
    }
}

// ==================== 通知 ====================
void platform_notify(const char* title, const char* body) {
    @autoreleasepool {
        NSString* t = [NSString stringWithUTF8String:(title ? title : "")];
        NSString* b = [NSString stringWithUTF8String:(body ? body : "")];
        NSString* script = [NSString stringWithFormat:
            @"display notification \"%@\" with title \"%@\"",
            [b stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""],
            [t stringByReplacingOccurrencesOfString:@"\"" withString:@"\\\""]];
        NSAppleScript* sc = [[NSAppleScript alloc] initWithSource:script];
        [sc executeAndReturnError:nil];
    }
}

// ==================== 配置目录 ====================
char* platform_config_dir() {
    @autoreleasepool {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(
            NSApplicationSupportDirectory, NSUserDomainMask, YES);
        NSString* base = paths[0];
        NSString* dir = [base stringByAppendingPathComponent:@"QuickPhrase"];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                 withIntermediateDirectories:YES
                                                  attributes:nil error:nil];
        return strdup([dir UTF8String]);
    }
}

// ==================== 初始化 ====================
void platform_init() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

        EventTypeSpec spec = { kEventClassKeyboard, kEventHotKeyPressed };
        InstallApplicationEventHandler(&hotkeyCallback, 1, &spec,
                                       NULL, &g_hotkeyHandlerRef);
    }
}

void platform_shutdown() {
    platform_unregister_all_hotkeys();
    @autoreleasepool {
        if (g_trayItem) {
            [[NSStatusBar systemStatusBar] removeStatusItem:g_trayItem];
            g_trayItem = nil;
            g_trayMenu = nil;
        }
    }
}

// ==================== 液态玻璃：透明窗口 + 毛玻璃 + 圆角裁剪 ====================

// 递归把所有子视图设为透明（普通 C++ 函数，不用 block，避免循环引用）
static void setViewTransparentRecursive(NSView* view) {
    if (!view) return;
    view.wantsLayer = YES;
    if (view.layer) {
        view.layer.opaque = NO;
        view.layer.backgroundColor = [[NSColor clearColor] CGColor];
    }
    for (NSView* sub in [view subviews]) {
        setViewTransparentRecursive(sub);
    }
}

void platform_make_window_transparent() {
    @autoreleasepool {
        NSWindow* window = [NSApp mainWindow];
        if (!window) {
            NSArray* windows = [NSApp windows];
            if ([windows count] > 0) window = [windows objectAtIndex:0];
        }
        if (!window) return;

        window.opaque = NO;
        window.backgroundColor = [NSColor clearColor];
        window.hasShadow = YES;

        NSView* contentView = window.contentView;
        if (!contentView) return;
        contentView.wantsLayer = YES;
        if (contentView.layer) {
            contentView.layer.opaque = NO;
            contentView.layer.backgroundColor = [[NSColor clearColor] CGColor];
        }

        // ★★★ 关键修复 1：让 SDL 内容被圆角裁剪，消除四角黑边 ★★★
        contentView.layer.cornerRadius = 14;
        contentView.layer.masksToBounds = YES;

        // 也给上层 NSThemeFrame 加圆角，防止窗口阴影边缘漏出
        NSView* themeFrame = [contentView superview];
        if (themeFrame) {
            themeFrame.wantsLayer = YES;
            themeFrame.layer.cornerRadius = 14;
            themeFrame.layer.masksToBounds = YES;
        }

        // 递归设置所有子视图透明
        setViewTransparentRecursive(contentView);

        // 检查是否已经添加过毛玻璃
        BOOL hasBlur = NO;
        for (NSView* sub in [contentView subviews]) {
            if ([sub isKindOfClass:[NSVisualEffectView class]]) {
                hasBlur = YES;
                break;
            }
        }
        if (hasBlur) return;

        NSVisualEffectView* blurView = [[NSVisualEffectView alloc]
            initWithFrame:contentView.bounds];
        blurView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        blurView.material = NSVisualEffectMaterialHUDWindow;
        blurView.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        blurView.state = NSVisualEffectStateActive;
        blurView.wantsLayer = YES;
        blurView.layer.cornerRadius = 14;
        blurView.layer.masksToBounds = YES;

        [contentView addSubview:blurView
                    positioned:NSWindowBelow
                    relativeTo:nil];
    }
}