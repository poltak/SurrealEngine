#include "cocoa_display_backend.h"
#include "cocoa_display_window.h"
#include "cocoa_open_file_dialog.h"
#include "cocoa_save_file_dialog.h"
#include "cocoa_open_folder_dialog.h"

#include <surrealwidgets/core/timer.h>

#include "AppKitWrapper.h"

// Helper struct to hold timer callbacks without requiring a Widget owner
struct CocoaTimerData
{
    std::function<void()> callback;
    NSTimer* nstimer;
};

@interface ZWidgetTimerTarget : NSObject
{
    CocoaTimerData* timerData;
}
- (id)initWithTimerData:(CocoaTimerData*)data;
- (void)timerFired:(NSTimer*)nstimer;
@end

@implementation ZWidgetTimerTarget
- (id)initWithTimerData:(CocoaTimerData*)data
{
    self = [super init];
    if (self)
    {
        timerData = data;
    }
    return self;
}

- (void)timerFired:(NSTimer*)nstimer
{
    if (timerData && timerData->callback)
        timerData->callback();
}
@end

// Quit in the menu asks the window in front to close, so its host can stop the program in its usual way
@interface ZWidgetMenuTarget : NSObject
- (void)quit:(id)sender;
@end

@implementation ZWidgetMenuTarget
- (void)quit:(id)sender
{
    NSWindow* window = [NSApp keyWindow] ? [NSApp keyWindow] : [NSApp mainWindow];
    if (!window)
    {
        for (NSWindow* candidate in [NSApp windows])
        {
            if ([candidate isVisible] && ([candidate styleMask] & NSWindowStyleMaskClosable))
            {
                window = candidate;
                break;
            }
        }
    }
    if (window)
        [window performClose:sender];
    else
        [NSApp terminate:sender];
}
@end

// The menu bar of a Mac program: Hide and Quit, Enter Full Screen (Control-Command-F, and Fn-F) and Minimize.
// A program with no menu bar has no shortcut for these, and a game window that holds the mouse leaves no other way.
static void CreateMainMenu()
{
    static ZWidgetMenuTarget* target = [[ZWidgetMenuTarget alloc] init];

    NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
    if (!name)
        name = [[NSProcessInfo processInfo] processName];

    NSMenu* mainMenu = [[NSMenu alloc] init];

    NSMenu* appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
    NSMenuItem* hideOthers = [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
    [hideOthers setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];
    [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quit = [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(quit:) keyEquivalent:@"q"];
    [quit setTarget:target];
    [[mainMenu addItemWithTitle:@"" action:nil keyEquivalent:@""] setSubmenu:appMenu];

    NSMenu* viewMenu = [[NSMenu alloc] initWithTitle:@"View"];
    NSMenuItem* fullscreen = [viewMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
    [fullscreen setKeyEquivalentModifierMask:NSEventModifierFlagControl | NSEventModifierFlagCommand];
    [[mainMenu addItemWithTitle:@"View" action:nil keyEquivalent:@""] setSubmenu:viewMenu];

    NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
    [[mainMenu addItemWithTitle:@"Window" action:nil keyEquivalent:@""] setSubmenu:windowMenu];

    [NSApp setMainMenu:mainMenu];
    [NSApp setWindowsMenu:windowMenu];

    // No tab bar items in the View menu
    [NSWindow setAllowsAutomaticWindowTabbing:NO];
}

std::unique_ptr<DisplayBackend> DisplayBackend::TryCreateCocoa()
{
    return std::make_unique<CocoaDisplayBackend>();
}

CocoaDisplayBackend::CocoaDisplayBackend()
{
    // Initialize NSApp if not already done
    [NSApplication sharedApplication];

    // Only configure NSApp if we're the owner (no delegate set yet)
    if ([NSApp delegate] == nil)
    {
        // We're the primary app - set activation policy for keyboard events.
        // A test with hidden windows is an accessory program: it has no icon in the Dock, it is not in the program
        // switcher, and macOS does not make it the active program at its start.
        [NSApp setActivationPolicy:CocoaHiddenWindows() ? NSApplicationActivationPolicyAccessory : NSApplicationActivationPolicyRegular];
        CreateMainMenu();
        [NSApp finishLaunching];
    }
    // else: Parent app is managing NSApp lifecycle, don't interfere
}

CocoaDisplayBackend::~CocoaDisplayBackend()
{
}

std::unique_ptr<DisplayWindow> CocoaDisplayBackend::Create(DisplayWindowHost* windowHost, WidgetType type, DisplayWindow* owner, RenderAPI renderAPI)
{
    return std::make_unique<CocoaDisplayWindow>(windowHost, type, owner, renderAPI);
}

void CocoaDisplayBackend::ProcessEvents()
{
    while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES])
    {
        [NSApp sendEvent:event];
    }
}

void CocoaDisplayBackend::RunLoop()
{
    [NSApp run];
}

void CocoaDisplayBackend::ExitLoop()
{
    [NSApp stop:nil];

    // Post a dummy event to wake up the event loop so stop: takes effect
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:event atStart:YES];
}

void* CocoaDisplayBackend::StartTimer(int timeoutMilliseconds, std::function<void()> onTimer)
{
    CocoaTimerData* timerData = new CocoaTimerData();
    timerData->callback = onTimer;

    ZWidgetTimerTarget* target = [[ZWidgetTimerTarget alloc] initWithTimerData:timerData];
    NSTimer* nstimer = [NSTimer scheduledTimerWithTimeInterval:timeoutMilliseconds / 1000.0
                                                        target:target
                                                      selector:@selector(timerFired:)
                                                      userInfo:nil
                                                       repeats:YES];
    timerData->nstimer = nstimer;

    return timerData;
}

void CocoaDisplayBackend::StopTimer(void* timerID)
{
    CocoaTimerData* timerData = static_cast<CocoaTimerData*>(timerID);
    if (timerData)
    {
        if (timerData->nstimer)
        {
            [timerData->nstimer invalidate];
        }
        delete timerData;
    }
}

Size CocoaDisplayBackend::GetScreenSize()
{
    NSRect screenFrame = [[NSScreen mainScreen] frame];
    return Size(screenFrame.size.width, screenFrame.size.height);
}

std::unique_ptr<OpenFileDialog> CocoaDisplayBackend::CreateOpenFileDialog(DisplayWindow* owner)
{
    return std::make_unique<CocoaOpenFileDialog>(owner);
}

std::unique_ptr<SaveFileDialog> CocoaDisplayBackend::CreateSaveFileDialog(DisplayWindow* owner)
{
    return std::make_unique<CocoaSaveFileDialog>(owner);
}

std::unique_ptr<OpenFolderDialog> CocoaDisplayBackend::CreateOpenFolderDialog(DisplayWindow* owner)
{
    return std::make_unique<CocoaOpenFolderDialog>(owner);
}
