// Implémentation macOS de PlatformWindow.h — le miroir Cocoa de
// Win32/Win32Window.cpp. La fenêtre porte une CAMetalLayer sur laquelle le
// backend Vulkan crée sa surface (VK_EXT_metal_surface via MoltenVK).
// Le décodage NSEvent → InputManager::feed() vit ici, comme le décodage
// WM_* côté Win32.

#include "Platform/PlatformWindow.h"

#include "Engine.h"  // WindowDesc
#include "InputManager.h"
#include "Renderer/Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_osx.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <array>
#include <mach-o/dyld.h>
#include <objc/runtime.h>

#include <string>
#include <vector>

namespace
{
// Équivalent du PostQuitMessage : levé à la fermeture de la fenêtre,
// consommé par platformPumpMessages.
bool g_quitRequested = false;

// L'Engine lié par platformBindContext — la pompe s'en sert pour router
// l'input (l'équivalent du GWLP_USERDATA du wndProc Win32).
batap::Engine* g_engine = nullptr;

// Virtual keycodes Carbon (indépendants de la disposition clavier) → Key.
// Valeurs de HIToolbox/Events.h (kVK_*), codées en dur pour ne pas tirer
// Carbon.framework.
const std::array<batap::Key, 128> KeycodeToKey = []
{
    using batap::Key;
    std::array<Key, 128> t{};
    t.fill(Key::Unknown);

    t[0x00] = Key::A;
    t[0x01] = Key::S;
    t[0x02] = Key::D;
    t[0x03] = Key::F;
    t[0x04] = Key::H;
    t[0x05] = Key::G;
    t[0x06] = Key::Z;
    t[0x07] = Key::X;
    t[0x08] = Key::C;
    t[0x09] = Key::V;
    t[0x0B] = Key::B;
    t[0x0C] = Key::Q;
    t[0x0D] = Key::W;
    t[0x0E] = Key::E;
    t[0x0F] = Key::R;
    t[0x10] = Key::Y;
    t[0x11] = Key::T;
    t[0x12] = Key::Num1;
    t[0x13] = Key::Num2;
    t[0x14] = Key::Num3;
    t[0x15] = Key::Num4;
    t[0x16] = Key::Num6;
    t[0x17] = Key::Num5;
    t[0x18] = Key::Equal;
    t[0x19] = Key::Num9;
    t[0x1A] = Key::Num7;
    t[0x1B] = Key::Minus;
    t[0x1C] = Key::Num8;
    t[0x1D] = Key::Num0;
    t[0x1E] = Key::RightBracket;
    t[0x1F] = Key::O;
    t[0x20] = Key::U;
    t[0x21] = Key::LeftBracket;
    t[0x22] = Key::I;
    t[0x23] = Key::P;
    t[0x24] = Key::Enter;
    t[0x25] = Key::L;
    t[0x26] = Key::J;
    t[0x27] = Key::Apostrophe;
    t[0x28] = Key::K;
    t[0x29] = Key::Semicolon;
    t[0x2A] = Key::Backslash;
    t[0x2B] = Key::Comma;
    t[0x2C] = Key::Slash;
    t[0x2D] = Key::N;
    t[0x2E] = Key::M;
    t[0x2F] = Key::Period;
    t[0x30] = Key::Tab;
    t[0x31] = Key::Space;
    t[0x32] = Key::Grave;
    t[0x33] = Key::Backspace;
    t[0x35] = Key::Escape;
    t[0x36] = Key::RSuper;
    t[0x37] = Key::LSuper;
    t[0x38] = Key::LShift;
    t[0x39] = Key::CapsLock;
    t[0x3A] = Key::LAlt;
    t[0x3B] = Key::LCtrl;
    t[0x3C] = Key::RShift;
    t[0x3D] = Key::RAlt;
    t[0x3E] = Key::RCtrl;
    t[0x40] = Key::F17;
    t[0x41] = Key::NumpadDecimal;
    t[0x43] = Key::NumpadMultiply;
    t[0x45] = Key::NumpadAdd;
    t[0x47] = Key::NumLock;  // "Clear" du pavé mac
    t[0x4B] = Key::NumpadDivide;
    t[0x4C] = Key::NumpadEnter;
    t[0x4E] = Key::NumpadSubtract;
    t[0x4F] = Key::F18;
    t[0x50] = Key::F19;
    t[0x52] = Key::Numpad0;
    t[0x53] = Key::Numpad1;
    t[0x54] = Key::Numpad2;
    t[0x55] = Key::Numpad3;
    t[0x56] = Key::Numpad4;
    t[0x57] = Key::Numpad5;
    t[0x58] = Key::Numpad6;
    t[0x59] = Key::Numpad7;
    t[0x5A] = Key::F20;
    t[0x5B] = Key::Numpad8;
    t[0x5C] = Key::Numpad9;
    t[0x60] = Key::F5;
    t[0x61] = Key::F6;
    t[0x62] = Key::F7;
    t[0x63] = Key::F3;
    t[0x64] = Key::F8;
    t[0x65] = Key::F9;
    t[0x67] = Key::F11;
    t[0x69] = Key::F13;
    t[0x6A] = Key::F16;
    t[0x6B] = Key::F14;
    t[0x6D] = Key::F10;
    t[0x6F] = Key::F12;
    t[0x71] = Key::F15;
    t[0x72] = Key::Insert;  // "Help" sur les vieux claviers mac
    t[0x73] = Key::Home;
    t[0x74] = Key::PageUp;
    t[0x75] = Key::Delete;  // forward delete
    t[0x76] = Key::F4;
    t[0x77] = Key::End;
    t[0x78] = Key::F2;
    t[0x79] = Key::PageDown;
    t[0x7A] = Key::F1;
    t[0x7B] = Key::ArrowLeft;
    t[0x7C] = Key::ArrowRight;
    t[0x7D] = Key::ArrowDown;
    t[0x7E] = Key::ArrowUp;

    return t;
}();

// Position souris en pixels physiques, origine en haut à gauche de la zone
// client — la convention du reste du moteur (et de Win32).
v2i mousePositionInWindow(NSEvent* event)
{
    NSWindow* window = event.window;
    if (!window)
        return {0, 0};
    NSView* view = window.contentView;
    const NSPoint p = [view convertPoint:event.locationInWindow fromView:nil];
    const double scale = window.backingScaleFactor;
    return {static_cast<int>(p.x * scale),
            static_cast<int>((view.bounds.size.height - p.y) * scale)};
}

void feedMouseMove(batap::InputManager& input, NSEvent* event)
{
    batap::InputManager::MouseEvent e{};
    e.Type = batap::InputManager::MouseEvent::Type::Move;
    // deltaY Cocoa est déjà orienté vers le bas — même sens que Win32
    NSWindow* window = event.window;
    const double scale = window ? window.backingScaleFactor : 1.0;
    e.Delta = {static_cast<int>(event.deltaX * scale),
               static_cast<int>(event.deltaY * scale)};
    e.ScreenPosition = mousePositionInWindow(event);
    input.feed(e);
}

void feedMouseClick(batap::InputManager& input, NSEvent* event, batap::MouseButton button,
                    batap::InputManager::KeyState state)
{
    batap::InputManager::MouseEvent e{};
    e.Type = batap::InputManager::MouseEvent::Type::Click;
    e.KeyState = state;
    e.Button = button;
    e.ScreenPosition = mousePositionInWindow(event);
    input.feed(e);
}

// true si l'événement est consommé par le moteur (ne pas le rendre à NSApp)
bool decodeEvent(batap::InputManager& input, NSEvent* event)
{
    using KeyState = batap::InputManager::KeyState;
    using batap::MouseButton;

    // Quand ImGui capture (champ texte, fenêtre survolée), le jeu ne voit pas
    // l'événement — le backend imgui_impl_osx le reçoit par son event monitor.
    const bool imguiWantsKeyboard =
        ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureKeyboard;
    const bool imguiWantsMouse = ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse;

    switch (event.type)
    {
        case NSEventTypeKeyDown:
        case NSEventTypeKeyUp: {
            if (imguiWantsKeyboard || event.keyCode >= KeycodeToKey.size())
                return true;
            const batap::Key key = KeycodeToKey[event.keyCode];
            if (key == batap::Key::Unknown)
                return true;
            input.feed(batap::InputManager::KeyEvent{
                event.type == NSEventTypeKeyDown ? KeyState::Pressed : KeyState::Released, key});
            // Consommé : sans ça NSApp bipe (« touche non gérée »). Cmd+Q et
            // les raccourcis du menu passent par performKeyEquivalent avant
            // d'arriver ici, ils ne sont pas perdus.
            return true;
        }

        // Les modificateurs n'émettent pas de KeyDown/KeyUp : un seul
        // flagsChanged par transition, le sens se déduit de l'état courant.
        case NSEventTypeFlagsChanged: {
            if (event.keyCode >= KeycodeToKey.size())
                return false;
            const batap::Key key = KeycodeToKey[event.keyCode];
            if (key == batap::Key::Unknown)
                return false;
            input.feed(batap::InputManager::KeyEvent{
                input.down(key) ? KeyState::Released : KeyState::Pressed, key});
            return false;
        }

        case NSEventTypeMouseMoved:
        case NSEventTypeLeftMouseDragged:
        case NSEventTypeRightMouseDragged:
        case NSEventTypeOtherMouseDragged:
            if (imguiWantsMouse)
                return false;
            feedMouseMove(input, event);
            return false;

        case NSEventTypeLeftMouseDown:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Left, KeyState::Pressed);
            return false;
        case NSEventTypeLeftMouseUp:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Left, KeyState::Released);
            return false;
        case NSEventTypeRightMouseDown:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Right, KeyState::Pressed);
            return false;
        case NSEventTypeRightMouseUp:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Right, KeyState::Released);
            return false;
        case NSEventTypeOtherMouseDown:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Middle, KeyState::Pressed);
            return false;
        case NSEventTypeOtherMouseUp:
            if (!imguiWantsMouse)
                feedMouseClick(input, event, MouseButton::Middle, KeyState::Released);
            return false;

        case NSEventTypeScrollWheel: {
            if (imguiWantsMouse)
                return false;
            batap::InputManager::MouseEvent e{};
            e.Type = batap::InputManager::MouseEvent::Type::Wheel;
            // Trackpad : deltas précis en points, ramenés à l'échelle des
            // crans de molette (~1.0 par geste franc, comme les 120 de Win32)
            e.Wheel = static_cast<float>(event.scrollingDeltaY) *
                      (event.hasPreciseScrollingDeltas ? 0.1f : 1.0f);
            e.ScreenPosition = mousePositionInWindow(event);
            input.feed(e);
            return false;
        }

        default:
            return false;
    }
}
}  // namespace

@interface BatapWindowDelegate : NSObject <NSWindowDelegate>
@property(assign, nonatomic) batap::Engine* engine;
@end

@implementation BatapWindowDelegate

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    g_quitRequested = true;
    return YES;
}

// Garde la drawableSize de la layer en phase avec la fenêtre (points × scale).
- (void)syncLayerSize:(NSWindow*)window
{
    NSView* view = window.contentView;
    CAMetalLayer* layer = (CAMetalLayer*)view.layer;
    layer.contentsScale = window.backingScaleFactor;
    layer.drawableSize = CGSizeMake(view.bounds.size.width * window.backingScaleFactor,
                                    view.bounds.size.height * window.backingScaleFactor);

    // La layer d'abord, le renderer ensuite : la recréation de swapchain lit
    // la taille de surface, qui suit drawableSize (MoltenVK).
    if (self.engine)
        self.engine->_renderer->resize(static_cast<uint32_t>(layer.drawableSize.width),
                                       static_cast<uint32_t>(layer.drawableSize.height));
}

- (void)windowDidResize:(NSNotification*)notification
{
    [self syncLayerSize:(NSWindow*)notification.object];
}

// Changement d'écran retina <-> non-retina
- (void)windowDidChangeBackingProperties:(NSNotification*)notification
{
    [self syncLayerSize:(NSWindow*)notification.object];
}

@end

namespace batap
{

void platformInit()
{
    [NSApplication sharedApplication];
    // Regular : icône dans le dock + fenêtre activable, même lancé du terminal
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
}

void* platformCreateWindow(const WindowDesc& desc)
{
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    NSWindow* window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, desc.width, desc.height)
                  styleMask:style
                    backing:NSBackingStoreBuffered
                      defer:NO];
    if (!window)
        return nullptr;

    [window setTitle:[NSString stringWithUTF8String:desc.title.c_str()]];
    [window center];

    NSView* view = window.contentView;
    view.wantsLayer = YES;
    view.layer = [CAMetalLayer layer];

    if (desc.transparent)
    {
        // Le compositeur lit l'alpha de la layer (cf. WindowDesc::transparent ;
        // côté swapchain : compositeAlpha POST_MULTIPLIED). L'ombre est coupée :
        // macOS la recalcule sur le contour opaque à chaque frame, c'est cher
        // et faux dès que le contenu bouge.
        window.opaque = NO;
        window.backgroundColor = [NSColor clearColor];
        window.hasShadow = NO;
        ((CAMetalLayer*)view.layer).opaque = NO;
    }

    BatapWindowDelegate* delegate = [BatapWindowDelegate new];
    window.delegate = delegate;
    // NSWindow ne retient pas son delegate — on l'attache à la fenêtre pour
    // qu'il vive aussi longtemps qu'elle.
    objc_setAssociatedObject(window, "batapDelegate", delegate,
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [delegate syncLayerSize:window];

    // La fenêtre vit pour toute la durée du process (comme le HWND côté
    // Win32, jamais détruit explicitement) : transfert de possession au void*.
    return (void*)CFBridgingRetain(window);
}

void platformBindContext(void* nativeHandle, Engine* engine)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;
    ((BatapWindowDelegate*)window.delegate).engine = engine;
    g_engine = engine;
}

void platformShowWindow(void* nativeHandle)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;

    static bool launched = false;
    if (!launched)
    {
        [NSApp finishLaunching];
        launched = true;
    }
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void platformSetWindowTitle(void* nativeHandle, const std::string& title)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;
    [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
}

void* platformSurfaceHandle(void* nativeHandle)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;
    return (__bridge void*)(CAMetalLayer*)window.contentView.layer;
}

void platformImGuiInit(void* nativeHandle)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;
    ImGui_ImplOSX_Init(window.contentView);
}

void platformImGuiNewFrame(void* nativeHandle)
{
    NSWindow* window = (__bridge NSWindow*)nativeHandle;
    ImGui_ImplOSX_NewFrame(window.contentView);
}

void platformImGuiShutdown()
{
    ImGui_ImplOSX_Shutdown();
}

bool platformPumpMessages()
{
    @autoreleasepool
    {
        NSEvent* event;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]))
        {
            if (g_engine && decodeEvent(*g_engine->_inputManager, event))
                continue;
            [NSApp sendEvent:event];
        }
    }
    return !g_quitRequested;
}

std::string platformExeDir()
{
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // demande la taille
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0)
        return {};
    // realpath : le chemin peut contenir des ../ selon le lanceur
    char resolved[PATH_MAX];
    if (::realpath(path.c_str(), resolved))
        path = resolved;
    const size_t slash = path.find_last_of('/');
    if (slash != std::string::npos)
        path.resize(slash);
    return path;
}

std::vector<std::string> platformCommandLineArgs()
{
    NSArray<NSString*>* arguments = NSProcessInfo.processInfo.arguments;
    std::vector<std::string> args;
    args.reserve(arguments.count > 0 ? arguments.count - 1 : 0);
    for (NSUInteger i = 1; i < arguments.count; ++i)
        args.emplace_back(arguments[i].UTF8String);
    return args;
}

}  // namespace batap
