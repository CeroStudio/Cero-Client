#include "bridge_internal.h"

int  local_bridge_port = 0;

int  g_game_fd = -1;
char g_mc_name[BRIDGE_NAME_MAX]   = {0};
char g_head_key[BRIDGE_HEAD_MAX]  = {0};
