#include "bridge_internal.h"
#include "../../../include/core/logger.h"
#include "../../../include/ipc/window_handlers.h"
#include "../../../include/discord/discord_rpc.h"
#include <string.h>

void bridge_handle_line(const char* line) {
    if (line == NULL || line[0] == '\0') return;

    log_msg("info", "[Bridge] <- jeu : %s\n", line);

    if (strncmp(line, "SHOW", 4) == 0 && (line[4] == '\0' || line[4] == ';')) {
        show_main_window();
    } else if (strncmp(line, "DISCORD_CLEAR", 13) == 0 && line[13] == '\0') {
        discord_rpc_clear();
    } else if (strncmp(line, "DISCORD;", 8) == 0) {
        bridge_handle_discord(line + 8);
    } else if (strncmp(line, "HELLO;", 6) == 0) {
        char info[BRIDGE_LINE_MAX];
        strncpy(info, line + 6, sizeof(info) - 1);
        info[sizeof(info) - 1] = '\0';

        char* semi1 = strchr(info, ';');
        char* semi2 = semi1 ? strchr(semi1 + 1, ';') : NULL;

        const char* pseudo = semi2 ? semi2 + 1 : NULL;

        log_msg("info", "[Bridge] Jeu connecté : %s\n", info);
        bridge_set_player_name(pseudo);

        if (semi1 && semi2) {
            *semi2 = '\0';
            bridge_presence_playing(semi1 + 1);
        }
    } else {
        log_msg("warn", "[Bridge] Message inconnu : %s\n", line);
    }
}
