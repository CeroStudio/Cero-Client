#include "../../include/platform/platform_defines.h"

#ifndef _WIN32
  #include <sys/stat.h>
#endif

#include "../../include/app/app.h"
#include "../../include/app/app_state.h"
#include "../../include/ipc/bridge.h"
#include "../../include/platform/single_instance.h"
#include "../../include/config/config.h"
#include "../../include/app/assets_loader.h"
#include "../../include/discord/discord_rpc.h"
#include "../../include/discord/rpc_helpers.h"
#include "../../include/ui/ui.h"
#include "../../include/ipc/window_handlers.h"
#include "../../include/ui/tray.h"
#include "../../include/ipc/game_handlers.h"
#include "../../include/ipc/instance_handlers.h"
#include "../../include/instances/instance.h"
#include "../../include/ipc/auth_handlers.h"
#include "../../include/ipc/settings_handlers.h"
#include "../../include/ipc/system_handlers.h"
#include "../../include/launch/launch_minecraft.h"
#include "../../include/platform/paths.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
#endif

int launcher_parse_args(int argc, char** argv, LauncherOptions* opts) {
    opts->assets_path = "assets.dat";
    opts->launch_version = NULL;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--customAssetsPath=", 19) == 0) {
            opts->assets_path = argv[i] + 19;
        } else if (strncmp(argv[i], "--launch=", 9) == 0 && argv[i][9] != '\0') {
            opts->launch_version = argv[i] + 9;
        }
    }
    return 1;
}

int launcher_init(const LauncherOptions* opts) {
    bridge_start();
    if (!opts->launch_version)
        single_instance_write_port(local_bridge_port);

    init_config();
    instances_init();

#ifndef _WIN32
    if (client_path[0] == '\0' || strcmp(client_path, "") == 0) {
        const char* home = getenv("HOME");
        if (home) {
            snprintf(client_path, MAX_PATH_SIZE, "%s/.ceroclient", home);
        } else {
            snprintf(client_path, MAX_PATH_SIZE, "./.ceroclient");
        }
        mkdir(client_path, 0755);
    }
#endif

    install_cleanup_handlers();

    log_msg("info", "Loading Assets into RAM from %s...\n", opts->assets_path);

    char exe_path[1024];
    get_exe_path(exe_path, sizeof(exe_path));

    if (!assets_load(opts->assets_path, exe_path)) {
        log_msg("error", "Failed to load %s\n", opts->assets_path);
        return 0;
    }

    log_msg("info", "Starting CeroClient ...\n");

    g_start_timestamp = (int64_t)time(NULL);

    return 1;
}

int launcher_create_ui(void) {
    char url[1024];
    snprintf(url, sizeof(url), "cero:///app/index.html");

    void* w = ui_create("CeroClient");
    if (!w) return 0;

#if defined(__linux__) || defined(__BSD__)
    ui_watch_system_theme(w);
#endif

    ui_set_frameless(w);

    const char* icon_path = NULL;
    ui_set_icon(w, icon_path);

    g_ui = w;
    ui_navigate(w, url);
    return 1;
}

void launcher_bind_ui(void) {
    void* w = g_ui;

    ui_bind(w, "launch_mc",     on_launch,        w);
    ui_bind(w, "drag_start",    on_drag_start,    w);
    ui_bind(w, "checkAccount",  on_check_account, w);
    ui_bind(w, "checkInternet", on_check_internet,w);
    ui_bind(w, "loginMicrosoft", on_login_microsoft, w);
    ui_bind(w, "logout_account", on_logout_account, w);
    ui_bind(w, "getAccount", on_get_account, w);
    ui_bind(w, "getMcToken", on_get_mc_token, w);
    ui_bind(w, "getVersion", on_get_version, w);
    ui_bind(w, "setVersion", on_set_version, w);
    ui_bind(w, "shell_open", on_shell_open, w);
    ui_bind(w, "get_settings",  on_get_settings,  w);
    ui_bind(w, "save_settings", on_save_settings, w);
    ui_bind(w, "minimize_window", on_minimize,      w);
    ui_bind(w, "show_window", on_show_window, w);
    ui_bind(w, "close_to_tray",   on_close_to_tray, w);
    ui_bind(w, "kill_game", on_kill_game, w);
    ui_bind(w, "quit_app", on_quit_app, w);
    ui_bind(w, "get_system_ram", on_get_system_ram, w);

    ui_bind(w, "list_instances", on_list_instances, w);
    ui_bind(w, "create_instance", on_create_instance, w);
    ui_bind(w, "delete_instance", on_delete_instance, w);
    ui_bind(w, "rename_instance", on_rename_instance, w);
    ui_bind(w, "set_instance_ram", on_set_instance_ram, w);
    ui_bind(w, "launch_instance", on_launch_instance, w);
    ui_bind(w, "kill_instance", on_kill_instance, w);
    ui_bind(w, "running_games", on_running_games, w);
    ui_bind(w, "get_mc_versions", on_get_mc_versions, w);
    ui_bind(w, "get_forge_versions", on_get_forge_versions, w);
    ui_bind(w, "get_fabric_versions", on_get_fabric_versions, w);
    ui_bind(w, "get_neoforge_versions", on_get_neoforge_versions, w);
    ui_bind(w, "get_quilt_versions", on_get_quilt_versions, w);

    ui_enable_js_console(w);
    ui_lockdown(w);
}

static void discord_rpc_start(void) {
    if (discord_rpc_init("1376179097113333881") == 0) {
        log_msg("info", "Discord RPC connected\n");
        rpc_set_launching();
    } else {
        log_msg("warn", "Discord not running or RPC failed\n");
    }
}

#ifdef _WIN32
static DWORD WINAPI discord_rpc_thread(LPVOID arg) {
    (void)arg;
    discord_rpc_start();
    return 0;
}
#else
static void* discord_rpc_thread(void* arg) {
    (void)arg;
    discord_rpc_start();
    return NULL;
}
#endif

void launcher_start_services(void) {
    window_platform_init(g_ui, NULL);
    
#ifdef _WIN32
    CreateThread(NULL, 0, discord_rpc_thread, NULL, 0, NULL);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, discord_rpc_thread, NULL);
    pthread_detach(tid);
#endif
}

void launcher_shutdown(void) {
    tray_shutdown();
    single_instance_release();
    discord_rpc_shutdown();
}