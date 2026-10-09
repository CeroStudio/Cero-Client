#include "../../include/launch/process_step.h"
#include "../../include/launch/game_registry.h"
#include "../../include/launch/launch_lock.h"
#include "../../include/utils/process.h"
#include "../../include/discord/rpc_helpers.h"
#include "../../include/ui/ui.h"
#include "../../include/core/logger.h"
#include <stdio.h>

static void emit_game_event(void* ui, const char* fn, const char* key) {
    if (!ui) return;

    char safe[64];
    size_t n = 0;
    for (const char* p = key ? key : "play"; *p && n + 1 < sizeof(safe); p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        safe[n++] = ok ? c : '_';
    }
    safe[n] = '\0';

    char js[192];
    snprintf(js, sizeof(js), "window.%s && window.%s(\"%s\")", fn, fn, safe);
    ui_eval(ui, js);
}

void run_game_process(const LaunchCtx* ctx, LaunchUserdata* ud,
                      const char* java_exe, const char** argv,
                      const char* version, const char* username, int is_fabric) {
    launch_report(ctx, "Lancement !", 100);
    log_msg("info", "Launching Minecraft %s as %s%s\n",
            version, username, is_fabric ? " (Fabric)" : "");

    const char* key = (ud && ud->key && ud->key[0]) ? ud->key : "play";
    unsigned    gen = ud ? ud->gen : 0;

    process_handle_t proc = PROCESS_HANDLE_INVALID;
    if (process_spawn(java_exe, argv, &proc) != 0) {
        launch_prep_unlock();
        launch_report(ctx, "Impossible de lancer le jeu", -1);
        log_msg("error", "Could not start the game process\n");
        return;
    }

    launch_prep_unlock();

    if (gen) game_registry_attach(key, gen, proc);

    if (ud && ud->ui) {
        if (ud->game_running) *ud->game_running = 1;
        emit_game_event(ud->ui, "_onGameStart", key);
    }

    int rc = process_wait(proc);

    if (gen) game_registry_finish(key, gen);
    process_close(proc);

    if (ud && ud->ui) {
        if (ud->game_running) *ud->game_running = 0;
        emit_game_event(ud->ui, "_onGameStop", key);
    }

    if (game_registry_running_count() == 0) rpc_set_idle();

    if (rc != 0) log_msg("error", "Game exited with code %d\n", rc);
    else         log_msg("succes", "Game exited cleanly\n");
}