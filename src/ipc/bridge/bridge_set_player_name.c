#include "bridge_internal.h"
#include "../../../include/core/logger.h"
#include <string.h>
#include <stdio.h>

void bridge_set_player_name(const char* name) {
    if (!name || !*name) return;

    bridge_game_lock();
    strncpy(g_mc_name, name, sizeof(g_mc_name) - 1);
    g_mc_name[sizeof(g_mc_name) - 1] = '\0';

    if (bridge_valid_mc_name(g_mc_name))
        snprintf(g_head_key, sizeof(g_head_key),
                 "https://mc-heads.net/avatar/%s/64", g_mc_name);
    else
        g_head_key[0] = '\0';
    bridge_game_unlock();

    log_msg("info", "[Bridge] Head URL: %s\n",
            g_head_key[0] ? g_head_key : "(aucune)");
}
