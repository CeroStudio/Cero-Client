#include "bridge_internal.h"
#include "../../../include/ipc/bridge.h"

const char* bridge_get_player_name(void) {
    return g_mc_name[0] ? g_mc_name : NULL;
}
