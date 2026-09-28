#include "../../include/ui/tray.h"
#include "../../include/core/logger.h"
#include <string.h>

#ifdef _WIN32

#include <shellapi.h>

#define WM_TRAY_ICON  (WM_APP + 1)
#define ID_TRAY_SHOW  2001
#define ID_TRAY_QUIT  2002

static NOTIFYICONDATAA g_nid = {0};
static HMENU           g_menu = NULL;
static tray_show_cb    g_on_show = NULL;
static tray_quit_cb    g_on_quit = NULL;
static int             g_added = 0;

int tray_init(HWND main_hwnd, const char* icon_path,
              tray_show_cb on_show, tray_quit_cb on_quit) {
    log_msg("info", "[TRAY] Initializing (Windows)...\n");
    log_msg("info", "[TRAY] HWND: %p\n", (void*)main_hwnd);
    log_msg("info", "[TRAY] Icon path: %s\n", icon_path ? icon_path : "(null)");

    g_on_show = on_show;
    g_on_quit = on_quit;

    HICON hIcon = NULL;
    if (icon_path) {
        hIcon = (HICON)LoadImageA(NULL, icon_path, IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE);
        log_msg("info", "[TRAY] LoadImage result: %p\n", (void*)hIcon);
    }
    if (!hIcon) {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
        log_msg("info", "[TRAY] Fallback to IDI_APPLICATION: %p\n", (void*)hIcon);
    }

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = main_hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon            = hIcon;
    strncpy(g_nid.szTip, "CeroClient", sizeof(g_nid.szTip) - 1);

    log_msg("info", "[TRAY] Calling Shell_NotifyIconA(NIM_ADD)...\n");
    if (!Shell_NotifyIconA(NIM_ADD, &g_nid)) {
        DWORD err = GetLastError();
        log_msg("error", "[TRAY] Shell_NotifyIconA failed! Error: %lu\n", err);
        return -1;
    }
    g_added = 1;
    log_msg("succes", "[TRAY] Shell_NotifyIconA succeeded\n");

    g_menu = CreatePopupMenu();
    if (!g_menu) {
        log_msg("error", "[TRAY] CreatePopupMenu failed!\n");
    } else {
        AppendMenuA(g_menu, MF_STRING, ID_TRAY_SHOW, "Show CeroClient");
        AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(g_menu, MF_STRING, ID_TRAY_QUIT, "Quit App");
        log_msg("info", "[TRAY] Menu created (3 items)\n");
    }

    log_msg("succes", "[TRAY] Initialized successfully\n");
    return 0;
}

void tray_shutdown(void) {
    log_msg("info", "[TRAY] Shutting down...\n");
    if (g_added) {
        log_msg("info", "[TRAY] Removing icon (NIM_DELETE)\n");
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        g_added = 0;
    }
    if (g_menu) {
        log_msg("info", "[TRAY] Destroying menu\n");
        DestroyMenu(g_menu);
        g_menu = NULL;
    }
    log_msg("info", "[TRAY] Shutdown complete\n");
}

int tray_handle_message(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* out) {
    if (msg == WM_TRAY_ICON) {
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) {
            log_msg("info", "[TRAY] Left click/double-click on icon\n");
            if (g_on_show) g_on_show();
            else log_msg("error", "[TRAY] g_on_show is NULL!\n");
        } else if (lp == WM_RBUTTONUP) {
            log_msg("info", "[TRAY] Right click - showing menu\n");
            POINT pt; GetCursorPos(&pt);
            log_msg("info", "[TRAY] Position: (%ld, %ld)\n", pt.x, pt.y);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(g_menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            PostMessageA(hwnd, WM_NULL, 0, 0);
        } else {
            log_msg("info", "[TRAY] WM_TRAY_ICON lp=0x%lx (ignored)\n", (unsigned long)lp);
        }
        *out = 0; return 1;
    }
    if (msg == WM_COMMAND) {
        WORD id = LOWORD(wp);
        log_msg("info", "[TRAY] WM_COMMAND id=%u\n", id);
        if (id == ID_TRAY_SHOW) {
            log_msg("info", "[TRAY] -> Action: Show\n");
            if (g_on_show) g_on_show();
            else log_msg("error", "[TRAY] g_on_show is NULL!\n");
            *out = 0; return 1;
        }
        if (id == ID_TRAY_QUIT) {
            log_msg("info", "[TRAY] -> Action: Quit\n");
            if (g_on_quit) g_on_quit();
            else log_msg("error", "[TRAY] g_on_quit is NULL!\n");
            *out = 0; return 1;
        }
    }
    return 0;
}

#elif defined(__APPLE__)

#else

#if !defined(HAVE_AYATANA) && !defined(HAVE_APPINDICATOR)

int tray_init(void* main_window, const char* icon_path,
              tray_show_cb on_show, tray_quit_cb on_quit) {
    (void)main_window; (void)icon_path; (void)on_show; (void)on_quit;
    log_msg("warn", "[TRAY] Disabled (no AYATANA or APPINDICATOR)\n");
    return 0;
}

void tray_shutdown(void) {
    log_msg("info", "[TRAY] Shutdown (no-op, disabled)\n");
}

#else

#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>

#if defined(HAVE_AYATANA)
  #include <libayatana-appindicator/app-indicator.h>
  #define INDICATOR_TYPE "Ayatana"
#elif defined(HAVE_APPINDICATOR)
  #include <libappindicator/app-indicator.h>
  #define INDICATOR_TYPE "AppIndicator"
#endif

static AppIndicator* g_indicator = NULL;
static GtkWidget* g_menu      = NULL;
static tray_show_cb  g_on_show   = NULL;
static tray_quit_cb  g_on_quit   = NULL;

static void on_show_clicked(GtkMenuItem* item, gpointer data) {
    (void)item; (void)data;
    log_msg("info", "[TRAY] Menu 'Show' clicked\n");
    if (g_on_show) g_on_show();
    else log_msg("error", "[TRAY] g_on_show is NULL!\n");
}

static void on_quit_clicked(GtkMenuItem* item, gpointer data) {
    (void)item; (void)data;
    log_msg("info", "[TRAY] Menu 'Quit' clicked\n");
    if (g_on_quit) g_on_quit();
    else log_msg("error", "[TRAY] g_on_quit is NULL!\n");
}

int tray_init(void* main_window, const char* icon_path,
              tray_show_cb on_show, tray_quit_cb on_quit) {
    (void)main_window;
    g_on_show = on_show;
    g_on_quit = on_quit;

    log_msg("info", "[TRAY] Initializing (Linux/%s)...\n", INDICATOR_TYPE);
    log_msg("info", "[TRAY] Icon path: %s\n", icon_path ? icon_path : "(null)");

    const char* icon_name = "ceroclient";
    char local_icon[512] = {0};
    int found = 0;

    if (icon_path && *icon_path) {
        const char* ext = strrchr(icon_path, '.');
        if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".svg") == 0)) {
            icon_name = icon_path;
            found = 1;
            log_msg("info", "[TRAY] Using param icon: %s\n", icon_path);
        }
    }

    if (!found) {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(local_icon, sizeof(local_icon),
                     "%s/.local/share/icons/hicolor/256x256/apps/ceroclient.png", home);
            if (access(local_icon, F_OK) == 0) {
                icon_name = local_icon;
                found = 1;
                log_msg("info", "[TRAY] Found icon: %s\n", local_icon);
            }
        }
    }

    if (!found) {
        char exe_path[512] = {0};
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len > 0) {
            exe_path[len] = '\0';
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(local_icon, sizeof(local_icon), "%s/icon.png", exe_path);
                if (access(local_icon, F_OK) == 0) {
                    icon_name = local_icon;
                    found = 1;
                    log_msg("info", "[TRAY] Found icon: %s\n", local_icon);
                }
            }
        }
    }

    if (!found) {
        icon_name = "applications-other";
        log_msg("warn", "[TRAY] No icon found, using default\n");
    }

    log_msg("info", "[TRAY] Final icon: %s\n", icon_name);

    log_msg("info", "[TRAY] Creating AppIndicator...\n");
    g_indicator = app_indicator_new("ceroclient",
                                    icon_name,
                                    APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    if (!g_indicator) {
        log_msg("error", "[TRAY] app_indicator_new returned NULL!\n");
        return -1;
    }
    log_msg("succes", "[TRAY] AppIndicator created: %p\n", (void*)g_indicator);

    app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(g_indicator, "CeroClient");
    app_indicator_set_icon_full(g_indicator, icon_name, "CeroClient");

    g_menu = gtk_menu_new();
    log_msg("info", "[TRAY] GTK menu created: %p\n", (void*)g_menu);

    GtkWidget* item_show = gtk_menu_item_new_with_label("Show CeroClient");
    g_signal_connect(item_show, "activate", G_CALLBACK(on_show_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), item_show);
    log_msg("info", "[TRAY] 'Show' item added: %p\n", (void*)item_show);

    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), sep);

    GtkWidget* item_quit = gtk_menu_item_new_with_label("Quit App");
    g_signal_connect(item_quit, "activate", G_CALLBACK(on_quit_clicked), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), item_quit);
    log_msg("info", "[TRAY] 'Quit' item added: %p\n", (void*)item_quit);

    gtk_widget_show_all(g_menu);
    app_indicator_set_menu(g_indicator, GTK_MENU(g_menu));

    log_msg("succes", "[TRAY] Initialized successfully\n");
    return 0;
}

void tray_shutdown(void) {
    log_msg("info", "[TRAY] Shutting down...\n");
    if (g_indicator) {
        log_msg("info", "[TRAY] Setting status to PASSIVE\n");
        app_indicator_set_status(g_indicator, APP_INDICATOR_STATUS_PASSIVE);
        g_object_unref(G_OBJECT(g_indicator));
        g_indicator = NULL;
    }
    g_menu = NULL;
    log_msg("info", "[TRAY] Shutdown complete\n");
}

#endif
#endif