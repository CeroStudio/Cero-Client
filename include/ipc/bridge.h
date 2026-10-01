#ifndef CERO_BRIDGE_H
#define CERO_BRIDGE_H

void bridge_start(void);

void bridge_send_to_game(const char* message);

const char* bridge_get_player_name(void);

const char* bridge_get_mc_head_key(void);

#endif