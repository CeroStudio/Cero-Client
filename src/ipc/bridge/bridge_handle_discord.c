#include "bridge_internal.h"
#include "../../../include/core/logger.h"
#include "../../../include/discord/discord_rpc.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

void bridge_handle_discord(const char* payload) {
    char buf[BRIDGE_LINE_MAX];
    strncpy(buf, payload, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* details = buf;
    char* state = NULL;
    char* ts_str = NULL;

    char* sep = strchr(buf, ';');
    if (sep) {
        *sep = '\0';
        state = sep + 1;
        char* sep2 = strchr(state, ';');
        if (sep2) {
            *sep2 = '\0';
            ts_str = sep2 + 1;
        }
    }

    if (details[0] == '\0') {
        log_msg("warn", "[Bridge] DISCORD sans details, ignore\n");
        return;
    }

    int64_t ts = (int64_t)time(NULL);
    if (ts_str != NULL) {
        char* end = NULL;
        long long parsed = strtoll(ts_str, &end, 10);
        if (end == ts_str || *end != '\0') {
            log_msg("warn", "[Bridge] Timestamp Discord invalide : %s\n", ts_str);
            return;
        }
        ts = (int64_t)parsed;
    }

    if (state != NULL && state[0] == '\0') state = NULL;

    char name[BRIDGE_NAME_MAX];
    char head[BRIDGE_HEAD_MAX];
    bridge_snapshot_player(name, sizeof(name), head, sizeof(head));

    discord_rpc_update(state, details, "logo", "CeroClient",
                       head[0] ? head : NULL,
                       name[0] ? name : NULL,
                       ts);
}
