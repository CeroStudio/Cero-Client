#include "bridge_internal.h"
#include "../../../include/discord/discord_rpc.h"
#include <stdio.h>
#include <time.h>

void bridge_presence_playing(const char* version) {
    char name[BRIDGE_NAME_MAX];
    char head[BRIDGE_HEAD_MAX];
    bridge_snapshot_player(name, sizeof(name), head, sizeof(head));

    char details[128];
    snprintf(details, sizeof(details), "Playing %s",
             (version && *version) ? version : "Minecraft");

    discord_rpc_update(NULL, details, "logo", "CeroClient",
                       head[0] ? head : NULL,
                       name[0] ? name : NULL,
                       (int64_t)time(NULL));
}