#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

#include "../../include/core/logger.h"
#include "webview/webview.h"

extern "C" {
    #include "../../include/app/assets_loader.h"
    #include "../../include/ui/tray.h"

    void quit_app(void);
    void show_main_window(void);
}

static NSString* cero_mime_for_path(NSString* path) {
    NSString* ext = [[path pathExtension] lowercaseString];
    if ([ext isEqualToString:@"html"] || [ext isEqualToString:@"htm"]) return @"text/html; charset=utf-8";
    if ([ext isEqualToString:@"js"] || [ext isEqualToString:@"mjs"])   return @"application/javascript; charset=utf-8";
    if ([ext isEqualToString:@"css"])   return @"text/css; charset=utf-8";
    if ([ext isEqualToString:@"json"])  return @"application/json; charset=utf-8";
    if ([ext isEqualToString:@"png"])   return @"image/png";
    if ([ext isEqualToString:@"jpg"] || [ext isEqualToString:@"jpeg"]) return @"image/jpeg";
    if ([ext isEqualToString:@"gif"])   return @"image/gif";
    if ([ext isEqualToString:@"webp"])  return @"image/webp";
    if ([ext isEqualToString:@"svg"])   return @"image/svg+xml";
    if ([ext isEqualToString:@"ico"])   return @"image/x-icon";
    if ([ext isEqualToString:@"woff2"]) return @"font/woff2";
    if ([ext isEqualToString:@"woff"])  return @"font/woff";
    if ([ext isEqualToString:@"ttf"])   return @"font/ttf";
    if ([ext isEqualToString:@"otf"])   return @"font/otf";
    if ([ext isEqualToString:@"wasm"])  return @"application/wasm";
    if ([ext isEqualToString:@"txt"])   return @"text/plain; charset=utf-8";
    return @"application/octet-stream";
}

static NSWindow* cero_get_nswindow(void* w) {
    if (!w) return nil;
    return (NSWindow*)webview_get_window((webview_t)w);
}

@interface CeroSchemeHandler : NSObject <WKURLSchemeHandler>
@end

@implementation CeroSchemeHandler

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView;
    NSURL *url = task.request.URL;

    NSString *path = url.path;
    while ([path hasPrefix:@"/"]) {
        path = [path substringFromIndex:1];
    }

    const uint8_t *data = NULL;
    size_t size = 0;
    int found = assets_get_file(path.UTF8String, &data, &size) && data;

    NSInteger status = found ? 200 : 404;
    NSString *mime = found ? cero_mime_for_path(path) : @"text/plain; charset=utf-8";

    if (!found) {
        log_msg("error", "[UI-macOS] asset introuvable: %s\n", path.UTF8String);
    }

    NSDictionary *headers = @{
        @"Content-Type": mime,
        @"Content-Length": [NSString stringWithFormat:@"%zu", found ? size : (size_t)0],
        @"Access-Control-Allow-Origin": @"*",
        @"Cache-Control": @"no-store",
    };

    NSHTTPURLResponse *response =
        [[NSHTTPURLResponse alloc] initWithURL:url
                                    statusCode:status
                                   HTTPVersion:@"HTTP/1.1"
                                  headerFields:headers];

    [task didReceiveResponse:response];
    if (found) {
        [task didReceiveData:[NSData dataWithBytes:data length:size]];
        assets_free_buffer(data);
    }
    [task didFinish];

#if !__has_feature(objc_arc)
    [response release];
#endif
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
    (void)webView; (void)task;
}

@end

static IMP g_orig_wkwebview_init = NULL;
static CeroSchemeHandler* g_scheme_handler = nil;

static id cero_wkwebview_init(id self, SEL _cmd, CGRect frame, WKWebViewConfiguration* config) {
    if (config && g_scheme_handler &&
        ![config urlSchemeHandlerForURLScheme:@"cero"]) {
        [config setURLSchemeHandler:g_scheme_handler forURLScheme:@"cero"];
        log_msg("info", "[UI-macOS] Scheme cero:// enregistré\n");
    }
    return ((id (*)(id, SEL, CGRect, id))g_orig_wkwebview_init)(self, _cmd, frame, config);
}

static void*         g_ui_handle     = NULL;
static tray_show_cb  g_tray_on_show  = NULL;
static tray_quit_cb  g_tray_on_quit  = NULL;

@interface CeroController : NSObject
- (void)quit:(id)sender;
- (void)showWindow:(id)sender;
- (void)hideWindow:(id)sender;
@end

@implementation CeroController
- (void)quit:(id)sender {
    (void)sender;
    if (g_tray_on_quit) g_tray_on_quit(); else quit_app();
}
- (void)showWindow:(id)sender {
    (void)sender;
    if (g_tray_on_show) g_tray_on_show(); else show_main_window();
}
- (void)hideWindow:(id)sender {
    (void)sender;
    NSWindow* win = cero_get_nswindow(g_ui_handle);
    if (win) [win orderOut:nil];
}
@end

static CeroController* g_controller = nil;
static NSStatusItem*   g_status_item = nil;

static NSMenuItem* cero_add_item(NSMenu* menu, NSString* title, SEL action,
                                 NSString* key, id target) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key];
    if (target) [item setTarget:target];
    [menu addItem:item];
#if !__has_feature(objc_arc)
    [item autorelease];
#endif
    return item;
}

static void cero_install_main_menu(void) {
    NSString* appName = [[NSProcessInfo processInfo] processName];
    if (!appName || [appName length] == 0) appName = @"CeroClient";

    NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@""];

    NSMenuItem* appItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [mainMenu addItem:appItem];
    NSMenu* appMenu = [[NSMenu alloc] initWithTitle:appName];
    [appItem setSubmenu:appMenu];

    cero_add_item(appMenu, [NSString stringWithFormat:@"About %@", appName],
                  @selector(orderFrontStandardAboutPanel:), @"", nil);
    [appMenu addItem:[NSMenuItem separatorItem]];
    cero_add_item(appMenu, [NSString stringWithFormat:@"Hide %@", appName],
                  @selector(hide:), @"h", nil);
    NSMenuItem* hideOthers = cero_add_item(appMenu, @"Hide Others",
                  @selector(hideOtherApplications:), @"h", nil);
    [hideOthers setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagOption)];
    cero_add_item(appMenu, @"Show All", @selector(unhideAllApplications:), @"", nil);
    [appMenu addItem:[NSMenuItem separatorItem]];
    cero_add_item(appMenu, [NSString stringWithFormat:@"Quit %@", appName],
                  @selector(quit:), @"q", g_controller);

    NSMenuItem* editItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
    [mainMenu addItem:editItem];
    NSMenu* editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [editItem setSubmenu:editMenu];

    cero_add_item(editMenu, @"Undo", @selector(undo:), @"z", nil);
    NSMenuItem* redo = cero_add_item(editMenu, @"Redo", @selector(redo:), @"z", nil);
    [redo setKeyEquivalentModifierMask:(NSEventModifierFlagCommand | NSEventModifierFlagShift)];
    [editMenu addItem:[NSMenuItem separatorItem]];
    cero_add_item(editMenu, @"Cut",        @selector(cut:),       @"x", nil);
    cero_add_item(editMenu, @"Copy",       @selector(copy:),      @"c", nil);
    cero_add_item(editMenu, @"Paste",      @selector(paste:),     @"v", nil);
    cero_add_item(editMenu, @"Select All", @selector(selectAll:), @"a", nil);

    NSMenuItem* winItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [mainMenu addItem:winItem];
    NSMenu* winMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    [winItem setSubmenu:winMenu];

    cero_add_item(winMenu, @"Minimize", @selector(performMiniaturize:), @"m", nil);
    cero_add_item(winMenu, @"Close",    @selector(hideWindow:),         @"w", g_controller);

    [NSApp setMainMenu:mainMenu];
    [NSApp setWindowsMenu:winMenu];

#if !__has_feature(objc_arc)
    [appItem release]; [appMenu release];
    [editItem release]; [editMenu release];
    [winItem release]; [winMenu release];
    [mainMenu release];
#endif
}


extern "C" {

void ui_macos_prepare(void) {
    if (g_orig_wkwebview_init) return;

    if (!g_scheme_handler) g_scheme_handler = [[CeroSchemeHandler alloc] init];

    Method m = class_getInstanceMethod([WKWebView class],
                                       @selector(initWithFrame:configuration:));
    if (!m) {
        log_msg("error", "[UI-macOS] initWithFrame:configuration: introuvable\n");
        return;
    }
    g_orig_wkwebview_init = method_setImplementation(m, (IMP)cero_wkwebview_init);
}

void ui_macos_post_create(void* w) {
    g_ui_handle = w;
    if (!g_controller) g_controller = [[CeroController alloc] init];

    cero_install_main_menu();

    NSWindow* win = cero_get_nswindow(w);
    if (win) {
        NSButton* closeBtn = [win standardWindowButton:NSWindowCloseButton];
        if (closeBtn) {
            [closeBtn setTarget:g_controller];
            [closeBtn setAction:@selector(hideWindow:)];
        }
    }

    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationDidBecomeActiveNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
        (void)note;
        NSWindow* cur = cero_get_nswindow(g_ui_handle);
        if (cur && ![cur isVisible] && ![cur isMiniaturized]) {
            [cur makeKeyAndOrderFront:nil];
        }
    }];
}

int tray_init(void* main_window, const char* icon_path,
              tray_show_cb on_show, tray_quit_cb on_quit) {
    (void)main_window; (void)icon_path;
    g_tray_on_show = on_show;
    g_tray_on_quit = on_quit;
    if (!g_controller) g_controller = [[CeroController alloc] init];

    g_status_item = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    if (!g_status_item) {
        log_msg("warn", "[TRAY] NSStatusItem indisponible\n");
        return -1;
    }
#if !__has_feature(objc_arc)
    [g_status_item retain];
#endif

    NSStatusBarButton* button = [g_status_item button];
    BOOL has_icon = NO;

    const uint8_t* data = NULL;
    size_t size = 0;
    if (assets_get_file("app/favicon.ico", &data, &size) && data && size > 0) {
        NSImage* img = [[NSImage alloc] initWithData:[NSData dataWithBytes:data length:size]];
        if (img) {
            [img setSize:NSMakeSize(18, 18)];
            if (button) { [button setImage:img]; has_icon = YES; }
#if !__has_feature(objc_arc)
            [img release];
#endif
        }
        assets_free_buffer(data);
    }
    if (!has_icon && button) [button setTitle:@"C"];

    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"CeroClient"];
    cero_add_item(menu, @"Show CeroClient", @selector(showWindow:), @"", g_controller);
    [menu addItem:[NSMenuItem separatorItem]];
    cero_add_item(menu, @"Quit CeroClient", @selector(quit:), @"", g_controller);
    [g_status_item setMenu:menu];
#if !__has_feature(objc_arc)
    [menu release];
#endif

    log_msg("info", "[TRAY] Status item macOS initialisé\n");
    return 0;
}

void tray_shutdown(void) {
    g_ui_handle = NULL;
    if (g_status_item) {
        [[NSStatusBar systemStatusBar] removeStatusItem:g_status_item];
#if !__has_feature(objc_arc)
        [g_status_item release];
#endif
        g_status_item = nil;
    }
}

void ui_show_window(void* w) {
    NSWindow* win = cero_get_nswindow(w);
    if (!win) return;
    if ([win isMiniaturized]) [win deminiaturize:nil];
    [win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void ui_hide_window(void* w) {
    NSWindow* win = cero_get_nswindow(w);
    if (!win) return;
    [win orderOut:nil];
}

void ui_minimize_window(void* w) {
    NSWindow* win = cero_get_nswindow(w);
    if (!win) return;
    [win miniaturize:nil];
}

void ui_set_frameless(void* w) {
    NSWindow* win = cero_get_nswindow(w);
    if (!win) return;

    win.titlebarAppearsTransparent = YES;
    win.titleVisibility = NSWindowTitleHidden;
    win.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

void ui_drag_start(void* w) {
    NSWindow* win = cero_get_nswindow(w);
    if (!win) return;
    NSEvent* ev = [NSApp currentEvent];
    if (ev) [win performWindowDragWithEvent:ev];
}

void ui_set_icon(void* w, const char* icon_path) {
    (void)w; (void)icon_path;

    const uint8_t* data = NULL;
    size_t size = 0;
    if (assets_get_file("app/favicon.ico", &data, &size) && data && size > 0) {
        NSData* nsdata = [NSData dataWithBytes:data length:size];
        NSImage* img = [[NSImage alloc] initWithData:nsdata];
        if (img) {
            [NSApp setApplicationIconImage:img];
#if !__has_feature(objc_arc)
            [img release];
#endif
        }
        assets_free_buffer(data);
    }
}

} /* extern "C" */
