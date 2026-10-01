#include "../../include/discord/rpc_helpers.h"
#include "../../include/app/app_state.h"
#include "../../include/discord/discord_rpc.h"
#include "../../include/ipc/bridge.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

static int valid_mc_name(const char* n) {
    size_t l = strlen(n);
    if (l < 1 || l > 16) return 0;
    for (size_t i = 0; i < l; i++) {
        if (!isalnum((unsigned char)n[i]) && n[i] != '_') return 0;
    }
    return 1;
}

static void mc_head_url(char* dst, size_t dsz, const char* mc_name) {
    if (!mc_name || !valid_mc_name(mc_name)) { dst[0] = '\0'; return; }
    snprintf(dst, dsz, "https://mc-heads.net/avatar/%s/64", mc_name);
}

void rpc_set_launching(void) {
    discord_rpc_update(NULL, "Launching...", "logo", "CeroClient",
                       NULL, NULL, g_start_timestamp);
}

void rpc_set_login(void) {
    discord_rpc_update(NULL, "In login menu", "logo", "CeroClient",
                       NULL, NULL, g_start_timestamp);
}

void rpc_set_idle(void) {
    discord_rpc_update(NULL, "Idling in menu", "logo", "CeroClient",
                       NULL, NULL, g_start_timestamp);
}

void rpc_set_playing(const char* version, const char* mc_name) {
    char details[128];
    snprintf(details, sizeof(details), "Playing %s", version ? version : "Minecraft");

    const char* name = (mc_name && *mc_name) ? mc_name : bridge_get_player_name();

    char head_key[256];
    mc_head_url(head_key, sizeof(head_key), name);

    discord_rpc_update(NULL, details, "logo", "CeroClient",
                       head_key[0] ? head_key : NULL,
                       name, (int64_t)time(NULL));
}