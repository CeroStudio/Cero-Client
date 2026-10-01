#include "bridge_internal.h"
#include <string.h>

void bridge_snapshot_player(char* name, size_t name_sz,
                            char* head, size_t head_sz) {
    bridge_game_lock();
    strncpy(name, g_mc_name, name_sz - 1);
    name[name_sz - 1] = '\0';
    strncpy(head, g_head_key, head_sz - 1);
    head[head_sz - 1] = '\0';
    bridge_game_unlock();
}
