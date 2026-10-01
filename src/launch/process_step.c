#include "../../include/launch/process_step.h"
#include "../../include/utils/process.h"
#include "../../include/discord/rpc_helpers.h"
#include "../../include/ui/ui.h"
#include "../../include/core/logger.h"
#include <stdio.h>

void run_game_process(const LaunchCtx* ctx, LaunchUserdata* ud,
                      const char* java_exe, const char** argv,
                      const char* version, const char* username, int is_fabric) {
    launch_report(ctx, "Lancement !", 100);
    log_msg("info", "Launching Minecraft %s as %s%s\n",
            version, username, is_fabric ? " (Fabric)" : "");

    if (ud && ud->ui && ud->game_running) {
        *ud->game_running = 1;
        ui_eval(ud->ui, "window._onGameStart && window._onGameStart()");
    }

    int rc = process_run(java_exe, argv);

    if (ud && ud->ui) {
        if (ud->game_running) *ud->game_running = 0;
        ui_eval(ud->ui, "window._onGameStop && window._onGameStop()");
    }

    rpc_set_idle();

    if (rc != 0) log_msg("error", "Game exited with code %d\n", rc);
    else         log_msg("succes", "Game exited cleanly\n");
}