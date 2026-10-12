#include "cocoa_display_window.h"
#include <stdio.h>
#include <vector>
#include <stdexcept>
#include <map>
#include <dlfcn.h>
#include <cmath>
#include <atomic>

// Minimal Vulkan type definitions (no headers required)
#ifndef VK_VERSION_1_0

#define VKAPI_CALL
#define VKAPI_PTR VKAPI_CALL

typedef uint32_t VkFlags;
typedef enum VkStructureType {
	VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT = 1000217000,
	VK_OBJECT_TYPE_MAX_ENUM = 0x7FFFFFFF
} VkStructureType;
typedef enum VkResult {
	VK_SUCCESS = 0,
	VK_RESULT_MAX_ENUM = 0x7FFFFFFF
} VkResult;
typedef struct VkAllocationCallbacks VkAllocationCallbacks;

typedef void (VKAPI_PTR* PFN_vkVoidFunction)(void);
typedef PFN_vkVoidFunction(VKAPI_PTR* PFN_vkGetInstanceProcAddr)(VkInstance instance, const char* pName);

#ifndef VK_EXT_metal_surface
typedef VkFlags VkMetalSurfaceCreateFlagsEXT;
typedef struct VkMetalSurfaceCreateInfoEXT {
	VkStructureType                sType;
	const void*                    pNext;
	VkMetalSurfaceCreateFlagsEXT   flags;
	const void*                    pLayer;  // CAMetalLayer*
} VkMetalSurfaceCreateInfoEXT;

typedef VkResult(VKAPI_PTR* PFN_vkCreateMetalSurfaceEXT)(
	VkInstance instance,
	const VkMetalSurfaceCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkSurfaceKHR* pSurface);
#endif

#endif

class CocoaVulkanLoader
{
public:
    CocoaVulkanLoader()
    {
        static const char* libraryNames[] = {
            "libvulkan.dylib",
            "libvulkan.1.dylib",
            "libMoltenVK.dylib"
        };

        // volk loads the active implementation RTLD_LOCAL, so, re-open only an
        // already loaded image so this never selects a second implementation
        for (const char* libraryName : libraryNames)
        {
            module = dlopen(libraryName, RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD);
            if (module)
                break;
        }
        if (!module)
            throw std::runtime_error("Could not access the active Vulkan implementation");

        getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module, "vkGetInstanceProcAddr"));
        if (!getInstanceProcAddr)
        {
            dlclose(module);
            module = nullptr;
            throw std::runtime_error("vkGetInstanceProcAddr not found in the active Vulkan implementation");
        }
    }

    ~CocoaVulkanLoader()
    {
        if (module)
            dlclose(module);
    }

    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    void* module = nullptr;
};

#include <surrealwidgets/core/image.h>
#include "surrealwidgets/window/cocoanativehandle.h"
#ifdef HAVE_METAL
#endif

#import "AppKitWrapper.h"
#import <Cocoa/Cocoa.h>

#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#ifdef HAVE_METAL
#import <Metal/Metal.h>
#endif

#ifdef HAVE_OPENGL
#define GL_SILENCE_DEPRECATION
#import <OpenGL/gl.h>
#import <OpenGL/glu.h>
#endif

// Forward declarations
@class ZWidgetView;

@class ZWidgetWindowDelegate;

// The virtual key codes of macOS name a place on the keyboard (the names are the ones of the US layout). A game
// binds its commands to places, so the layout of the user does not change them. Text input does not use this
// table: it comes from insertText.
// The modifier keys are not in the table. They come from flagsChanged, and Shift and Control become InputKey::Shift
// and InputKey::Ctrl, the keys that Windows gives a game for the left and for the right key.
InputKey keycode_to_inputkey(unsigned short keycode)
{
    static const std::map<unsigned short, InputKey> keymap =
    {
        // Letters
        {0, InputKey::A}, {1, InputKey::S}, {2, InputKey::D}, {3, InputKey::F}, {4, InputKey::H},
        {5, InputKey::G}, {6, InputKey::Z}, {7, InputKey::X}, {8, InputKey::C}, {9, InputKey::V},
        {11, InputKey::B}, {12, InputKey::Q}, {13, InputKey::W}, {14, InputKey::E}, {15, InputKey::R},
        {16, InputKey::Y}, {17, InputKey::T}, {31, InputKey::O}, {32, InputKey::U}, {34, InputKey::I},
        {35, InputKey::P}, {37, InputKey::L}, {38, InputKey::J}, {40, InputKey::K}, {45, InputKey::N},
        {46, InputKey::M},
        // Number row
        {18, InputKey::_1}, {19, InputKey::_2}, {20, InputKey::_3}, {21, InputKey::_4}, {23, InputKey::_5},
        {22, InputKey::_6}, {26, InputKey::_7}, {28, InputKey::_8}, {25, InputKey::_9}, {29, InputKey::_0},
        // Punctuation. Key 10 is the key left of the 1 key on an ISO keyboard.
        {27, InputKey::Minus}, {24, InputKey::Equals}, {33, InputKey::LeftBracket}, {30, InputKey::RightBracket},
        {42, InputKey::Backslash}, {41, InputKey::Semicolon}, {39, InputKey::SingleQuote}, {43, InputKey::Comma},
        {47, InputKey::Period}, {44, InputKey::Slash}, {50, InputKey::Tilde}, {10, InputKey::Tilde},
        // Control keys
        {36, InputKey::Enter}, {48, InputKey::Tab}, {49, InputKey::Space}, {51, InputKey::Backspace},
        {53, InputKey::Escape},
        // Navigation. A PC keyboard gives key 114 (Help) for its Insert key.
        {114, InputKey::Insert}, {115, InputKey::Home}, {116, InputKey::PageUp}, {117, InputKey::Delete},
        {119, InputKey::End}, {121, InputKey::PageDown},
        {123, InputKey::Left}, {124, InputKey::Right}, {125, InputKey::Down}, {126, InputKey::Up},
        // Function keys
        {122, InputKey::F1}, {120, InputKey::F2}, {99, InputKey::F3}, {118, InputKey::F4}, {96, InputKey::F5},
        {97, InputKey::F6}, {98, InputKey::F7}, {100, InputKey::F8}, {101, InputKey::F9}, {109, InputKey::F10},
        {103, InputKey::F11}, {111, InputKey::F12}, {105, InputKey::F13}, {107, InputKey::F14},
        {113, InputKey::F15}, {106, InputKey::F16}, {64, InputKey::F17}, {79, InputKey::F18},
        {80, InputKey::F19}, {90, InputKey::F20},
        // Keypad
        {82, InputKey::NumPad0}, {83, InputKey::NumPad1}, {84, InputKey::NumPad2}, {85, InputKey::NumPad3},
        {86, InputKey::NumPad4}, {87, InputKey::NumPad5}, {88, InputKey::NumPad6}, {89, InputKey::NumPad7},
        {91, InputKey::NumPad8}, {92, InputKey::NumPad9}, {65, InputKey::NumPadPeriod}, {67, InputKey::GreyStar},
        {69, InputKey::GreyPlus}, {78, InputKey::GreyMinus}, {75, InputKey::GreySlash}, {76, InputKey::Enter},
        {81, InputKey::Equals}, {71, InputKey::NumLock}
    };

    auto it = keymap.find(keycode);
    if (it != keymap.end())
    {
        return it->second;
    }
    return InputKey::None;
}

struct CocoaDisplayWindowLifetime
{
    std::atomic<bool> alive = true;
    std::atomic<bool> updatePending = false;
};

struct CocoaDisplayWindowImpl
{
public:
    DisplayWindowHost* windowHost = nullptr;
    NSWindow* window = nil;
    ZWidgetWindowDelegate* delegate = nil;
    NSBitmapImageRep* bitmapRep = nil;
    CGImageRef cgImage = nullptr;
    std::map<InputKey, bool> keyState;
    bool cursorLocked = false;
    bool resizable = true;
    NSSize minContentSize = NSMakeSize(0.0, 0.0);
    NSSize maxContentSize = NSMakeSize(CGFLOAT_MAX, CGFLOAT_MAX);
    RenderAPI renderAPI = RenderAPI::Unspecified;
    std::shared_ptr<CocoaDisplayWindowLifetime> lifetime = std::make_shared<CocoaDisplayWindowLifetime>();

    CVDisplayLinkRef displayLink = nullptr;

    CGColorSpaceRef colorSpace = nullptr;
    CGContextRef bitmapContext = nullptr;
    std::vector<uint32_t> pixelBuffer;
    int bitmapWidth = 0;
    int bitmapHeight = 0;

    CAMetalLayer* metalLayer = nil;
#ifdef HAVE_METAL
    id<MTLDevice> metalDevice = nil;
#endif

#ifdef HAVE_OPENGL
    NSOpenGLContext* openglContext = nil;
#endif

    // Declare methods, but implement them outside the struct
    void initOpenGL(ZWidgetView* view);
    void updateDrawableSize(CGSize size);

    CVReturn displayLinkOutputCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* inNow, const CVTimeStamp* inOutputTime, CVOptionFlags flagsIn, CVOptionFlags* flagsOut, void* displayLinkContext);
    void startDisplayLink();
    void stopDisplayLink();

    ~CocoaDisplayWindowImpl()
    {
        stopDisplayLink();
        if (bitmapContext) CGContextRelease(bitmapContext);
        if (colorSpace) CGColorSpaceRelease(colorSpace);
        if (cgImage) CGImageRelease(cgImage);
    }
};

static CVReturn DisplayLinkOutputCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* inNow, const CVTimeStamp* inOutputTime, CVOptionFlags flagsIn, CVOptionFlags* flagsOut, void* displayLinkContext)
{
    CocoaDisplayWindowImpl* impl = (CocoaDisplayWindowImpl*)displayLinkContext;
    return impl->displayLinkOutputCallback(displayLink, inNow, inOutputTime, flagsIn, flagsOut, displayLinkContext);
}

void CocoaDisplayWindowImpl::startDisplayLink()
{
    if (displayLink) return;

    CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
    CVDisplayLinkSetOutputCallback(displayLink, &DisplayLinkOutputCallback, this);
    CVDisplayLinkStart(displayLink);
}

void CocoaDisplayWindowImpl::stopDisplayLink()
{
    if (!displayLink) return;

    CVDisplayLinkStop(displayLink);
    CVDisplayLinkRelease(displayLink);
    displayLink = nullptr;
}

@interface ZWidgetView : NSView
{
@public
    CocoaDisplayWindowImpl* impl;
    NSTrackingArea* trackingArea;
}
- (id)initWithImpl:(CocoaDisplayWindowImpl*)impl;
@end

@implementation ZWidgetView

- (id)initWithImpl:(CocoaDisplayWindowImpl*)d
{
    self = [super init];
    if (self)
    {
        impl = d;
        // Vulkan via MoltenVK and Metal both render through a CAMetalLayer.
        // Bitmap rendering uses the backing layer's contents for reliable
        // presentation on modern macOS
        self.wantsLayer = YES;
        self.layer.contentsScale = [NSScreen mainScreen].backingScaleFactor;
        // Don't create tracking area here - it will be created in updateTrackingAreas
        trackingArea = nil;
    }
    return self;
}

- (void)dealloc
{
    if (trackingArea) {
        [self removeTrackingArea:trackingArea];
        trackingArea = nil;
    }

#if !__has_feature(objc_arc)
    [super dealloc];
#endif
}

- (BOOL)isOpaque
{
    return YES;
}

- (void)updateTrackingAreas
{
    if (trackingArea) {
        [self removeTrackingArea:trackingArea];
        trackingArea = nil;
    }

    trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                                  owner:self
                                               userInfo:nil];
    [self addTrackingArea:trackingArea];
    [super updateTrackingAreas];
}

- (CALayer *)makeBackingLayer
{
    // Vulkan via MoltenVK and Metal both require a CAMetalLayer
    if (impl && (impl->renderAPI == RenderAPI::Vulkan || impl->renderAPI == RenderAPI::Metal))
    {
        return [CAMetalLayer layer];
    }
    return [CALayer layer];  // For OpenGL
}
- (BOOL)canBecomeKeyView
{
    return YES;
}

- (void)setFrame:(NSRect)frame
{
    [super setFrame:frame];
    if (impl && (impl->renderAPI == RenderAPI::Vulkan || impl->renderAPI == RenderAPI::Metal))
    {
        impl->updateDrawableSize(frame.size);
    }
}
- (NSView *)hitTest:(NSPoint)point
{
    return [super hitTest:point];
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;  // Allow mouse events even if window isn't key
}

- (BOOL)becomeFirstResponder
{
    return [super becomeFirstResponder];
}

- (BOOL)resignFirstResponder
{
    return [super resignFirstResponder];
}

- (void)setNeedsDisplay:(BOOL)flag
{
    [super setNeedsDisplay:flag];
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (!impl) return;

    if (impl->renderAPI == RenderAPI::Bitmap || impl->renderAPI == RenderAPI::Unspecified)
    {
        // Just draw the existing bitmap (Update() handles regeneration asynchronously)
        if (impl->cgImage)
        {
            CGContextRef context = (CGContextRef)[[NSGraphicsContext currentContext] graphicsPort];
            if (!context) {
                return;
            }
            CGContextDrawImage(context, NSRectToCGRect(self.bounds), impl->cgImage);
        }
    }
    else if (impl->renderAPI == RenderAPI::OpenGL)
    {
#ifdef HAVE_OPENGL
        if (impl->openglContext)
        {
            [impl->openglContext makeCurrentContext];
            [impl->openglContext flushBuffer];
        }
#endif
    }
    else if (impl->renderAPI == RenderAPI::Vulkan || impl->renderAPI == RenderAPI::Metal)
    {
        // Vulkan and Metal rendering are performed outside drawRect:.
    }
}

- (void)viewDidMoveToWindow
{
    if ([self window])
    {
        // Make this view the first responder to receive mouse/keyboard events
        [[self window] makeFirstResponder:self];

        impl->windowHost->OnWindowPaint();
        // Vulkan is driven by the engine's render loop. The display link is only
        // needed by the existing OpenGL/Metal paths.
        if (impl->renderAPI == RenderAPI::OpenGL || impl->renderAPI == RenderAPI::Metal)
        {
            impl->startDisplayLink();
        }
    }
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    if (!newWindow)
    {
        impl->stopDisplayLink();
    }
}

- (void)mouseEntered:(NSEvent *)theEvent
{
    // Not used yet
}

- (void)mouseExited:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
        impl->windowHost->OnWindowMouseLeave();
}

- (void)dispatchMouseMove:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        if (impl->cursorLocked)
        {
            impl->windowHost->OnWindowRawMouseMove((int)[theEvent deltaX], (int)[theEvent deltaY]);
        }
        else
        {
            NSPoint p = [self convertPoint:[theEvent locationInWindow] fromView:nil];
            impl->windowHost->OnWindowMouseMove(Point(p.x, self.bounds.size.height - p.y));
        }
    }
}

- (void)mouseMoved:(NSEvent *)theEvent
{
    [self dispatchMouseMove:theEvent];
}

- (void)mouseDown:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSPoint p = [theEvent locationInWindow];
        double flippedY = [self frame].size.height - p.y;

        InputKey mouseKey = InputKey::None;
        if ([theEvent buttonNumber] == 0) mouseKey = InputKey::LeftMouse;
        else if ([theEvent buttonNumber] == 1) mouseKey = InputKey::RightMouse;
        else if ([theEvent buttonNumber] == 2) mouseKey = InputKey::MiddleMouse;

        if (mouseKey != InputKey::None)
        {
            impl->windowHost->OnWindowMouseDown(Point(p.x, flippedY), mouseKey);
        }
    }
}

- (void)mouseUp:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSPoint p = [theEvent locationInWindow];
        double flippedY = [self frame].size.height - p.y;

        InputKey mouseKey = InputKey::None;
        if ([theEvent buttonNumber] == 0) mouseKey = InputKey::LeftMouse;
        else if ([theEvent buttonNumber] == 1) mouseKey = InputKey::RightMouse;
        else if ([theEvent buttonNumber] == 2) mouseKey = InputKey::MiddleMouse;

        if (mouseKey != InputKey::None)
        {
            impl->windowHost->OnWindowMouseUp(Point(p.x, flippedY), mouseKey);
        }
    }
}

- (void)rightMouseDown:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSPoint p = [theEvent locationInWindow];
        impl->windowHost->OnWindowMouseDown(Point(p.x, [self frame].size.height - p.y), InputKey::RightMouse);
    }
}

- (void)rightMouseUp:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSPoint p = [theEvent locationInWindow];
        impl->windowHost->OnWindowMouseUp(Point(p.x, [self frame].size.height - p.y), InputKey::RightMouse);
    }
}

- (void)mouseDragged:(NSEvent *)theEvent
{
    [self dispatchMouseMove:theEvent];
}

- (void)rightMouseDragged:(NSEvent *)theEvent
{
    [self dispatchMouseMove:theEvent];
}

- (void)otherMouseDragged:(NSEvent *)theEvent
{
    [self dispatchMouseMove:theEvent];
}

- (void)scrollWheel:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSPoint p = [theEvent locationInWindow];
        if ([theEvent deltaY] > 0)
            impl->windowHost->OnWindowMouseWheel(Point(p.x, [self frame].size.height - p.y), InputKey::MouseWheelUp);
        else if ([theEvent deltaY] < 0)
            impl->windowHost->OnWindowMouseWheel(Point(p.x, [self frame].size.height - p.y), InputKey::MouseWheelDown);
    }

}

- (void)keyDown:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        InputKey key = keycode_to_inputkey([theEvent keyCode]);
        if (key != InputKey::None)
        {
            impl->keyState[key] = true;
            impl->windowHost->OnWindowKeyDown(key);
        }
    }

    // Call interpretKeyEvents to trigger insertText: for text input
    [self interpretKeyEvents:@[theEvent]];
}

// Text input protocol methods - required for keyboard input on macOS
- (void)insertText:(id)string
{
    if (impl && impl->windowHost)
    {
        NSString* str = [string isKindOfClass:[NSAttributedString class]] ?
                        [(NSAttributedString*)string string] : (NSString*)string;
        impl->windowHost->OnWindowKeyChar([str UTF8String]);
    }
}

- (void)doCommandBySelector:(SEL)selector
{
    // This handles special keys like Enter, Tab, etc.
    // They're already handled by keyDown, so we can ignore them here
}

- (void)keyUp:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        InputKey key = keycode_to_inputkey([theEvent keyCode]);
        if (key != InputKey::None)
        {
            impl->keyState[key] = false;
            impl->windowHost->OnWindowKeyUp(key);
        }
    }

}

- (void)flagsChanged:(NSEvent *)theEvent
{
    if (impl && impl->windowHost)
    {
        NSEventModifierFlags flags = [theEvent modifierFlags];
        bool shiftPressed = (flags & NSEventModifierFlagShift) != 0;
        bool ctrlPressed = (flags & NSEventModifierFlagControl) != 0;

        // A game gets one Shift key and one Ctrl key, as on Windows, where the key event of the left and of the
        // right key has the same key code. The key bindings of a game use these two keys.
        auto update = [&](InputKey key, bool pressed)
        {
            if (impl->keyState[key] != pressed)
            {
                impl->keyState[key] = pressed;
                if (pressed) impl->windowHost->OnWindowKeyDown(key);
                else impl->windowHost->OnWindowKeyUp(key);
            }
        };
        update(InputKey::Shift, shiftPressed);
        update(InputKey::Ctrl, ctrlPressed);
        update(InputKey::Alt, (flags & NSEventModifierFlagOption) != 0);
        update(InputKey::LCommand, (flags & NSEventModifierFlagCommand) != 0);

        // The flags do not tell the left key from the right key. Keep the state of the two the same.
        impl->keyState[InputKey::LShift] = shiftPressed;
        impl->keyState[InputKey::RShift] = shiftPressed;
        impl->keyState[InputKey::LControl] = ctrlPressed;
        impl->keyState[InputKey::RControl] = ctrlPressed;

        // The Caps Lock key (key code 57) gives one event for each press, when its light changes. A game takes
        // that as one press and release of the key.
        if ([theEvent keyCode] == 57)
        {
            impl->windowHost->OnWindowKeyDown(InputKey::CapsLock);
            impl->windowHost->OnWindowKeyUp(InputKey::CapsLock);
        }
    }

}
@end

// ZWidgetWindowDelegate interface and implementation
@interface ZWidgetWindowDelegate : NSObject <NSWindowDelegate>
{
    CocoaDisplayWindowImpl* impl;
}
- (id)initWithImpl:(CocoaDisplayWindowImpl*)impl;
@end

@implementation ZWidgetWindowDelegate
- (id)initWithImpl:(CocoaDisplayWindowImpl*)d
{
    self = [super init];
    if (self)
    {
        impl = d;
    }
    return self;
}

- (void)windowDidResize:(NSNotification *)notification
{
    // Throttle resize events to prevent excessive calls that can cause crashes
    static int resizeCount = 0;
    static NSTimeInterval lastResizeTime = 0;
    NSTimeInterval now = [[NSDate date] timeIntervalSince1970];

    if (now - lastResizeTime < 0.01) {
        resizeCount++;
        if (resizeCount > 5) {
            return;  // Throttle: too many rapid calls
        }
    } else {
        resizeCount = 0;
    }
    lastResizeTime = now;

    if (impl && impl->windowHost)
    {
        impl->windowHost->OnWindowGeometryChanged();
    }
}
@end

void CocoaDisplayWindowImpl::initOpenGL(ZWidgetView* view)
{
#ifdef HAVE_OPENGL
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    NSOpenGLPixelFormatAttribute attrs[] = { NSOpenGLPFAAccelerated, NSOpenGLPFANoRecovery, NSOpenGLPFADoubleBuffer, NSOpenGLPFAColorSize, 24, NSOpenGLPFAAlphaSize, 8, NSOpenGLPFADepthSize, 24, 0 };
    NSOpenGLPixelFormat* pixelFormat = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
    openglContext = [[NSOpenGLContext alloc] initWithFormat:pixelFormat shareContext:nil];
    if (openglContext)
    {
        renderAPI = RenderAPI::OpenGL;
        [openglContext setView:(NSView*)view];
    }
    else
    {
        renderAPI = RenderAPI::Bitmap; // Fallback if OpenGL not available
    }
#pragma clang diagnostic pop
#else
    renderAPI = RenderAPI::Bitmap; // Fallback if OpenGL not available
#endif
}

void CocoaDisplayWindowImpl::updateDrawableSize(CGSize size)
{
    if (metalLayer)
    {
        CGFloat scale = window ? window.backingScaleFactor : [NSScreen mainScreen].backingScaleFactor;
        metalLayer.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
    }
}

CVReturn CocoaDisplayWindowImpl::displayLinkOutputCallback(CVDisplayLinkRef displayLink, const CVTimeStamp* inNow, const CVTimeStamp* inOutputTime, CVOptionFlags flagsIn, CVOptionFlags* flagsOut, void* displayLinkContext)
{
    // Note: displayLink is only started for OpenGL/Metal rendering
    // Bitmap rendering is event-driven and doesn't use displayLink
    if (renderAPI == RenderAPI::Metal)
    {
#ifdef HAVE_METAL
        if (windowHost)
        {
            windowHost->OnWindowPaint();
        }
#endif
    }
    else if (renderAPI == RenderAPI::OpenGL)
    {
#ifdef HAVE_OPENGL
        if (openglContext && windowHost)
        {
            [openglContext makeCurrentContext];
            windowHost->OnWindowPaint();
            [openglContext flushBuffer];
        }
#endif
    }
    return kCVReturnSuccess;
}

CocoaDisplayWindow::CocoaDisplayWindow(DisplayWindowHost* windowHost, WidgetType type, DisplayWindow* owner, RenderAPI renderAPI)
{
    impl = std::make_unique<CocoaDisplayWindowImpl>();
    impl->windowHost = windowHost;
    impl->renderAPI = renderAPI;

    NSRect contentRect = NSMakeRect(0, 0, 640, 480);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable;
    if (type == WidgetType::Popup)
        style = NSWindowStyleMaskBorderless;

    impl->window = [[NSWindow alloc] initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    impl->delegate = [[ZWidgetWindowDelegate alloc] initWithImpl:impl.get()];
    [impl->window setDelegate:impl->delegate];
    [impl->window setCollectionBehavior:NSWindowCollectionBehaviorFullScreenPrimary];
    [impl->window setAcceptsMouseMovedEvents:YES];
    [impl->window setRestorable:NO];
    [impl->window setReleasedWhenClosed:NO];

    ZWidgetView* view = [[ZWidgetView alloc] initWithImpl:impl.get()];
    [impl->window setContentView:view];

    if (owner)
    {
        CocoaDisplayWindow* cocoaOwner = static_cast<CocoaDisplayWindow*>(owner);
        [impl->window setParentWindow:cocoaOwner->impl->window];
    }

    // Accessing view.layer forces layer-backing, so only do it for APIs that
    // render through Core Animation. Let the view create the CAMetalLayer so
    // it remains the layer delegate, recommended by the MoltenVK docs
    if (renderAPI == RenderAPI::Vulkan || renderAPI == RenderAPI::Metal)
    {
        impl->metalLayer = (CAMetalLayer*)view.layer;
        impl->metalLayer.contentsScale = impl->window.backingScaleFactor;
        impl->updateDrawableSize(view.bounds.size);

        if (renderAPI == RenderAPI::Metal)
        {
#ifdef HAVE_METAL
            impl->metalDevice = MTLCreateSystemDefaultDevice();
            if (impl->metalDevice)
            {
                impl->metalLayer.device = impl->metalDevice;
                impl->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
                impl->metalLayer.framebufferOnly = NO;  // Allow reading for screenshots, etc.
                impl->metalLayer.presentsWithTransaction = NO;
                impl->metalLayer.displaySyncEnabled = YES;
                impl->metalLayer.maximumDrawableCount = 3;
            }
            else
#endif
            {
                impl->renderAPI = RenderAPI::Bitmap;
            }
        }
    }
#ifdef HAVE_OPENGL
    if (renderAPI == RenderAPI::OpenGL)
    {
        impl->initOpenGL(view);
    }
    else
#endif
    if (renderAPI != RenderAPI::Vulkan && renderAPI != RenderAPI::Metal)
    {
        impl->renderAPI = RenderAPI::Bitmap;
    }
}

CocoaDisplayWindow::~CocoaDisplayWindow()
{
    if (!impl)
        return;

    impl->lifetime->alive = false;
    impl->windowHost = nullptr;

    if (impl->cursorLocked)
    {
        impl->cursorLocked = false;
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }

    if (impl->window)
    {
        ZWidgetView* view = (ZWidgetView*)[impl->window contentView];
        view->impl = nullptr;
        [impl->window setDelegate:nil];
        [impl->window orderOut:nil];
        [impl->window close];
        impl->window = nil;
    }
    impl->delegate = nil;
}

void CocoaDisplayWindow::SetWindowTitle(const std::string& text)
{
    if (impl->window)
    {
        [impl->window setTitle:[NSString stringWithUTF8String:text.c_str()]];
    }
}

void CocoaDisplayWindow::SetWindowIcon(const std::vector<std::shared_ptr<Image>>& images)
{
    if (impl->window && !images.empty())
    {
    // For simplicity, use the first image as the icon.
        // A more robust implementation might choose an appropriate size.
        std::shared_ptr<Image> iconImage = images[0];

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            iconImage->GetData(), iconImage->GetWidth(), iconImage->GetHeight(), 8, iconImage->GetWidth() * 4, colorSpace,
            kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big);

        if (context)
        {
            CGImageRef cgImage = CGBitmapContextCreateImage(context);
            if (cgImage)
            {
                NSImage* nsImage = [[NSImage alloc] initWithCGImage:cgImage size:NSZeroSize];
                [NSApp setApplicationIconImage:nsImage];
                CGImageRelease(cgImage);
            }
            CGContextRelease(context);
        }
        CGColorSpaceRelease(colorSpace);
    }
}

/*
void CocoaDisplayWindow::SetWindowFrame(const Rect& box)
{
    if (impl->window)
    {
        NSRect frame = NSMakeRect(box.x, [[NSScreen mainScreen] frame].size.height - box.y - box.height, box.width, box.height);
        [impl->window setFrame:frame display:YES animate:NO];
    }
}
*/

void CocoaDisplayWindow::SetClientFrame(const Rect& box)
{
    if (impl->window)
    {
	// Tbd: should this be done using frameRectForContentRect? Using the method below is not atomic

        NSRect contentRect = NSMakeRect(box.x, [[NSScreen mainScreen] frame].size.height - box.y - box.height, box.width, box.height);
        [impl->window setContentSize:contentRect.size];
        [impl->window setFrameOrigin:contentRect.origin];
    }
}

Rect CocoaDisplayWindow::GetClientFrame() const
{
    if (impl->window)
    {
        NSRect frame = [impl->window contentRectForFrameRect:[impl->window frame]];
        return Rect(frame.origin.x, [[NSScreen mainScreen] frame].size.height - frame.origin.y - frame.size.height, frame.size.width, frame.size.height);
    }
    return {};
}

Size CocoaDisplayWindow::GetClientSize() const
{
    if (impl->window)
    {
        NSRect contentRect = [[impl->window contentView] frame];
        return Size(contentRect.size.width, contentRect.size.height);
    }
    return {};
}

// A test runner sets SURREAL_NO_ACTIVATE so that a test window does not take the keyboard focus from the person at
// the computer. The window then opens behind the windows of the active app.
static bool AllowAppActivation()
{
    static bool allow = getenv("SURREAL_NO_ACTIVATE") == nullptr;
    return allow;
}

void CocoaDisplayWindow::Show()
{
    if (impl->window)
    {
        // CRITICAL: Activate the application so it can receive keyboard events
        if (AllowAppActivation())
            [NSApp activateIgnoringOtherApps:YES];
        [impl->window makeKeyAndOrderFront:nil];

        // Trigger initial layout
        if (impl->windowHost)
        {
            impl->windowHost->OnWindowGeometryChanged();
            // Paint to create CGImage before any drawRect calls
            impl->windowHost->OnWindowPaint();
        }
        // Just trigger a redraw without regenerating (bitmap already created above)
        [[impl->window contentView] setNeedsDisplay:YES];
    }
}

void CocoaDisplayWindow::ShowFullscreen()
{
    // toggleFullScreen changes the state in the two directions, so do not call it for a window that is fullscreen already
    if (impl->window && !IsWindowFullscreen())
    {
        [impl->window toggleFullScreen:nil];
    }
}

void CocoaDisplayWindow::ShowMaximized()
{
    if (impl->window)
    {
        [impl->window zoom:nil];
    }
}

void CocoaDisplayWindow::ShowMinimized()
{
    if (impl->window)
    {
        [impl->window miniaturize:nil];
    }
}

void CocoaDisplayWindow::ShowNormal()
{
    if (impl->window)
    {
        if (IsWindowFullscreen())
        {
            // Leave the fullscreen space. macOS puts the frame back that the window had before.
            [impl->window toggleFullScreen:nil];
            return;
        }
        if ([impl->window isMiniaturized])
        {
            [impl->window deminiaturize:nil];
        }
        if ([impl->window isZoomed])
        {
            [impl->window zoom:nil];
        }
    }
}

void CocoaDisplayWindow::SetWindowResizable(bool enable)
{
    impl->resizable = enable;

    if (!impl->window)
        return;

    // Borderless popups have no resize affordances to begin with
    if (([impl->window styleMask] & NSWindowStyleMaskTitled) != 0)
    {
        NSWindowStyleMask style = [impl->window styleMask];
        if (enable)
            style |= NSWindowStyleMaskResizable;
        else
            style &= ~NSWindowStyleMaskResizable;
        [impl->window setStyleMask:style];

        // The zoom button survives the style mask change, so disable it too
        [[impl->window standardWindowButton:NSWindowZoomButton] setEnabled:enable];
        [impl->window setCollectionBehavior:enable ? NSWindowCollectionBehaviorFullScreenPrimary : NSWindowCollectionBehaviorFullScreenNone];
    }

    if (enable)
    {
        [impl->window setContentMinSize:impl->minContentSize];
        [impl->window setContentMaxSize:impl->maxContentSize];
    }
    else
    {
        // Pin the window to its current content size
        NSSize size = [[impl->window contentView] frame].size;
        [impl->window setContentMinSize:size];
        [impl->window setContentMaxSize:size];
    }
}

void CocoaDisplayWindow::SetWindowMinSize(int width, int height)
{
    impl->minContentSize = NSMakeSize(width, height);

    if (impl->window && impl->resizable)
    {
        [impl->window setContentMinSize:impl->minContentSize];
    }
}

void CocoaDisplayWindow::SetWindowMaxSize(int width, int height)
{
    impl->maxContentSize = NSMakeSize(width, height);

    if (impl->window && impl->resizable)
    {
        [impl->window setContentMaxSize:impl->maxContentSize];
    }
}

bool CocoaDisplayWindow::IsWindowFullscreen()
{
    if (impl->window)
    {
        return ([impl->window styleMask] & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen;
    }
    return false;
}

void CocoaDisplayWindow::Hide()
{
    if (impl->window)
    {
        [impl->window orderOut:nil];
    }
}

void CocoaDisplayWindow::Activate()
{
    if (impl->window)
    {
        [impl->window makeKeyAndOrderFront:nil];
        if (AllowAppActivation())
            [NSApp activateIgnoringOtherApps:YES];
    }
}

void CocoaDisplayWindow::ShowCursor(bool enable)
{
    if (enable)
    {
        [NSCursor unhide];
    }
    else
    {
        [NSCursor hide];
    }
}

// No-ops as MacOS doesn't allow this
void CocoaDisplayWindow::LockKeyboard() {}

void CocoaDisplayWindow::UnlockKeyboard() {}

void CocoaDisplayWindow::LockCursor()
{
    if (impl->window && !impl->cursorLocked)
    {
        impl->cursorLocked = true;
        CGAssociateMouseAndMouseCursorPosition(false);
        [NSCursor hide];
    }
}

void CocoaDisplayWindow::UnlockCursor()
{
    if (impl->window && impl->cursorLocked)
    {
        impl->cursorLocked = false;
        CGAssociateMouseAndMouseCursorPosition(true);
        [NSCursor unhide];
    }
}

void CocoaDisplayWindow::CaptureMouse()
{
}

void CocoaDisplayWindow::ReleaseMouseCapture()
{
}

void CocoaDisplayWindow::Update()
{
    if (!impl->window || !impl->windowHost)
        return;

    // Guard the raw DisplayWindowHost pointer because the NSWindow can outlive
    // its C++ owner, so merely capturing it weakly is not enough to make a
    // queued paint safe after the owner is destroyed.
    std::shared_ptr<CocoaDisplayWindowLifetime> lifetime = impl->lifetime;
    if (lifetime->updatePending.exchange(true))
        return;

    __weak NSWindow* weakWindow = impl->window;
    DisplayWindowHost* hostPtr = impl->windowHost;

    dispatch_async(dispatch_get_main_queue(), ^{
        lifetime->updatePending = false;
        if (!lifetime->alive)
            return;

        NSWindow* strongWindow = weakWindow;
        if (!strongWindow)
            return;

        hostPtr->OnWindowPaint();
    });
}

bool CocoaDisplayWindow::GetKeyState(InputKey key)
{
    auto it = impl->keyState.find(key);
    if (it != impl->keyState.end())
    {
        return it->second;
    }
    return false;
}

void CocoaDisplayWindow::SetCursor(StandardCursor cursor, std::shared_ptr<CustomCursor> custom)
{
    NSCursor* nsCursor = nil;

    if (custom && !custom->GetFrames().empty())
    {
        // Create custom cursor from image
        const auto& frame = custom->GetFrames()[0];  // For now, use first frame (TODO: animated cursors)
        auto image = frame.FrameImage;
        Point hotspot = custom->GetHotspot();

        // Convert Image to NSImage
        int width = image->GetWidth();
        int height = image->GetHeight();
        const uint32_t* pixels = (const uint32_t*)image->GetData();

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            (void*)pixels, width, height, 8, width * 4, colorSpace,
            kCGImageAlphaPremultipliedLast | (CGBitmapInfo)kCGBitmapByteOrder32Big);

        if (context)
        {
            CGImageRef cgImage = CGBitmapContextCreateImage(context);
            if (cgImage)
            {
                NSImage* nsImage = [[NSImage alloc] initWithCGImage:cgImage size:NSMakeSize(width, height)];
                NSPoint nsHotspot = NSMakePoint(hotspot.x, hotspot.y);
                nsCursor = [[NSCursor alloc] initWithImage:nsImage hotSpot:nsHotspot];
                CGImageRelease(cgImage);
            }
            CGContextRelease(context);
        }
        CGColorSpaceRelease(colorSpace);
    }
    else
    {
        // Standard cursors
        switch (cursor)
        {
            case StandardCursor::arrow: nsCursor = [NSCursor arrowCursor]; break;
            case StandardCursor::ibeam: nsCursor = [NSCursor IBeamCursor]; break;
            case StandardCursor::wait: nsCursor = [NSCursor operationNotAllowedCursor]; break;
            case StandardCursor::cross: nsCursor = [NSCursor crosshairCursor]; break;
            case StandardCursor::size_nwse: nsCursor = [NSCursor resizeUpCursor]; break;
            case StandardCursor::size_nesw: nsCursor = [NSCursor resizeDownCursor]; break;
            case StandardCursor::size_we: nsCursor = [NSCursor resizeLeftRightCursor]; break;
            case StandardCursor::size_ns: nsCursor = [NSCursor resizeUpDownCursor]; break;
            case StandardCursor::size_all: nsCursor = [NSCursor openHandCursor]; break;
            case StandardCursor::no: nsCursor = [NSCursor operationNotAllowedCursor]; break;
            case StandardCursor::hand: nsCursor = [NSCursor pointingHandCursor]; break;
            default: nsCursor = [NSCursor arrowCursor]; break;
        }
    }

    if (nsCursor)
    {
        [nsCursor set];
    }
}

int CocoaDisplayWindow::GetPixelWidth() const
{
    if (impl->window)
    {
        NSRect contentRect = [[impl->window contentView] frame];
        CGFloat scale = [impl->window backingScaleFactor];
        if (scale <= 0.0) scale = 1.0;
        return (int)std::round(contentRect.size.width * scale);
    }
    return 0;
}

int CocoaDisplayWindow::GetPixelHeight() const
{
    if (impl->window)
    {
        NSRect contentRect = [[impl->window contentView] frame];
        CGFloat scale = [impl->window backingScaleFactor];
        if (scale <= 0.0) scale = 1.0;
        return (int)std::round(contentRect.size.height * scale);
    }
    return 0;
}

double CocoaDisplayWindow::GetDpiScale() const
{
    if (impl->window)
    {
        CGFloat scale = [impl->window backingScaleFactor];
        if (scale <= 0.0) scale = 1.0;
        return scale;
    }
    return 1.0;
}

Point CocoaDisplayWindow::MapFromGlobal(const Point& pos) const
{
    if (impl->window)
    {
        NSPoint globalPoint = NSMakePoint(pos.x, [[NSScreen mainScreen] frame].size.height - pos.y);
        NSPoint windowPoint = [impl->window convertPointFromScreen:globalPoint];
        NSPoint viewPoint = [[impl->window contentView] convertPoint:windowPoint fromView:nil];
        return Point(viewPoint.x, [[impl->window contentView] frame].size.height - viewPoint.y);
    }
    return {};
}

Point CocoaDisplayWindow::MapToGlobal(const Point& pos) const
{
    if (impl->window)
    {
        NSPoint viewPoint = NSMakePoint(pos.x, [[impl->window contentView] frame].size.height - pos.y);
        NSPoint windowPoint = [[impl->window contentView] convertPoint:viewPoint toView:nil];
        NSPoint globalPoint = [impl->window convertPointToScreen:windowPoint];
        return Point(globalPoint.x, [[NSScreen mainScreen] frame].size.height - globalPoint.y);
    }
    return {};
}

void CocoaDisplayWindow::SetBorderColor(uint32_t rgba8) {}
void CocoaDisplayWindow::SetCaptionColor(uint32_t rgba8) {}
void CocoaDisplayWindow::SetCaptionTextColor(uint32_t rgba8) {}

void CocoaDisplayWindow::PresentBitmap(int width, int height, const uint32_t* pixels)
{
    // ZWidget uses simple bitmap blitting for launcher UI
    // Metal layer is available for applications via GetMetalLayer()
    if (impl->renderAPI == RenderAPI::OpenGL)
    {
#ifdef HAVE_OPENGL
        if (impl->openglContext)
        {
            [impl->openglContext makeCurrentContext];
            glViewport(0, 0, width, height);
            glRasterPos2i(-1, -1);
            glDrawPixels(width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
            // No flushBuffer here, it will be done by the display link callback
        }
#endif
    }
    else if (impl->renderAPI == RenderAPI::Bitmap || impl->renderAPI == RenderAPI::Unspecified)
    {
        if (impl->cgImage) CGImageRelease(impl->cgImage);
        impl->cgImage = nullptr;

        // Copy pixel data to persistent buffer since the input pointer is temporary
        size_t dataSize = width * height * 4;
        impl->pixelBuffer.resize(width * height);
        memcpy(impl->pixelBuffer.data(), pixels, dataSize);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(
            impl->pixelBuffer.data(), width, height, 8, width * 4, colorSpace,
            kCGImageAlphaPremultipliedFirst | (CGBitmapInfo)kCGBitmapByteOrder32Little);

        if (context)
        {
            impl->cgImage = CGBitmapContextCreateImage(context);
            CGContextRelease(context);
        }
        CGColorSpaceRelease(colorSpace);

        if (impl->window)
        {
            NSView* contentView = [impl->window contentView];
            [CATransaction begin];
            [CATransaction setDisableActions:YES];
            contentView.layer.contents = (__bridge id)impl->cgImage;
            contentView.layer.contentsGravity = kCAGravityResize;
            [CATransaction commit];
        }
    }
}

std::string CocoaDisplayWindow::GetClipboardText()
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    if (text)
    {
        return [text UTF8String];
    }
    return {};
}

void CocoaDisplayWindow::SetClipboardText(const std::string& text)
{
    NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
    [pasteboard clearContents];
    [pasteboard setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
}

// For tests: make a key event of macOS and give it to the window as the system does. The event then goes through
// the same code as a key of the real keyboard: the first responder of the window, the key table, the window host.
// It does not need the keyboard focus. type: 0 = key down, 1 = key up, 2 = change of the modifier flags.
void CocoaDisplayWindow::InjectNativeKeyEvent(int type, int keyCode, unsigned int modifierFlags, const std::string& characters)
{
    if (!impl->window)
        return;
    NSEventType eventType = type == 0 ? NSEventTypeKeyDown : type == 1 ? NSEventTypeKeyUp : NSEventTypeFlagsChanged;
    NSString* chars = [NSString stringWithUTF8String:characters.c_str()];
    if (!chars)
        chars = @"";
    NSEvent* event = [NSEvent keyEventWithType:eventType
                                      location:NSZeroPoint
                                 modifierFlags:(NSEventModifierFlags)modifierFlags
                                     timestamp:[[NSProcessInfo processInfo] systemUptime]
                                  windowNumber:[impl->window windowNumber]
                                       context:nil
                                    characters:chars
                   charactersIgnoringModifiers:chars
                                     isARepeat:NO
                                       keyCode:(unsigned short)keyCode];
    if (event)
        [impl->window sendEvent:event];
}

// For tests: make a mouse event of macOS and give it to a window as the system does. The event then goes through the
// same code as the real mouse: the hit test of the window, the view, the window host.
// type: 0 = left button down, 1 = left button up, 2 = mouse move.
void CocoaDisplayWindow::InjectNativeMouseEvent(int type, double x, double y)
{
    if (!impl->window)
        return;

    // The place on the screen. The view has its origin at the bottom left.
    NSView* view = [impl->window contentView];
    NSPoint viewPoint = NSMakePoint(x, [view bounds].size.height - y);
    NSPoint screenPoint = [impl->window convertPointToScreen:[view convertPoint:viewPoint toView:nil]];

    // A real click goes to the front window at that place. The list of a dropdown is a window of its own.
    NSWindow* target = nil;
    for (NSWindow* candidate in [NSApp orderedWindows])
    {
        if ([candidate isVisible] && NSPointInRect(screenPoint, [candidate frame]))
        {
            target = candidate;
            break;
        }
    }
    if (!target)
        return;

    NSEventType eventType = type == 0 ? NSEventTypeLeftMouseDown : type == 1 ? NSEventTypeLeftMouseUp : NSEventTypeMouseMoved;
    NSEvent* event = [NSEvent mouseEventWithType:eventType
                                        location:[target convertPointFromScreen:screenPoint]
                                   modifierFlags:0
                                       timestamp:[[NSProcessInfo processInfo] systemUptime]
                                    windowNumber:[target windowNumber]
                                         context:nil
                                     eventNumber:0
                                      clickCount:1
                                        pressure:(type == 0 ? 1.0f : 0.0f)];
    if (event)
        [target sendEvent:event];
}

void* CocoaDisplayWindow::GetNativeHandle()
{
    // Create and return a CocoaNativeHandle for this window
    CocoaNativeHandle* handle = new CocoaNativeHandle();
    if (impl->window)
    {
        handle->nsWindow = impl->window;
        handle->nsView = [impl->window contentView];
        handle->metalLayer = (__bridge void*)impl->metalLayer;
    }
    return handle;
}


std::vector<std::string> CocoaDisplayWindow::GetVulkanInstanceExtensions()
{
    std::vector<std::string> extensions;
    extensions.push_back("VK_KHR_surface");
    extensions.push_back("VK_EXT_metal_surface");
    return extensions;
}
VkSurfaceKHR CocoaDisplayWindow::CreateVulkanSurface(VkInstance instance)
{
    if (!impl->window || !impl->metalLayer)
        throw std::runtime_error("Could not create vulkan surface: no metal layer");

    // Query through the same Vulkan implementation that volk used to create
    // this instance, rather than opening a separate loader
    static CocoaVulkanLoader loader;

    auto createMetalSurface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(loader.getInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!createMetalSurface)
        throw std::runtime_error("Could not create vulkan surface: vkCreateMetalSurfaceEXT not found");

    VkMetalSurfaceCreateInfoEXT surfaceInfo = {};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    surfaceInfo.pLayer = (__bridge void*)impl->metalLayer;

    VkSurfaceKHR surface = nullptr;
    VkResult err = createMetalSurface(instance, &surfaceInfo, nullptr, &surface);
    if (err != VK_SUCCESS)
        throw std::runtime_error("Could not create vulkan surface: vkCreateMetalSurfaceEXT failed");
    return surface;
}

void* CocoaDisplayWindow::GetMetalDevice()
{
#ifdef HAVE_METAL
    // Return the Metal device for application rendering
    return (__bridge void*)impl->metalDevice;
#else
    throw std::runtime_error("Metal support not compiled into zwidget");
#endif
}

void* CocoaDisplayWindow::GetMetalLayer()
{
    return (__bridge void*)impl->metalLayer;
}
