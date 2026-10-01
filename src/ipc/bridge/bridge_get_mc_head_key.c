#include "bridge_internal.h"
#include "../../../include/ipc/bridge.h"

const char* bridge_get_mc_head_key(void) {
    return g_head_key[0] ? g_head_key : NULL;
}
