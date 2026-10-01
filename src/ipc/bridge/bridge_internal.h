#ifndef CERO_BRIDGE_INTERNAL_H
#define CERO_BRIDGE_INTERNAL_H

#include "../../../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <pthread.h>
#endif

#include <stddef.h>
#include <stdint.h>

#define BRIDGE_LINE_MAX 512
#define BRIDGE_NAME_MAX 64
#define BRIDGE_HEAD_MAX 256

extern int  local_bridge_port;
extern int  g_game_fd;
extern char g_mc_name[BRIDGE_NAME_MAX];
extern char g_head_key[BRIDGE_HEAD_MAX];

void bridge_lock_init(void);
void bridge_game_lock(void);
void bridge_game_unlock(void);

int  bridge_valid_mc_name(const char* name);
void bridge_set_player_name(const char* name);
void bridge_snapshot_player(char* name, size_t name_sz,
                            char* head, size_t head_sz);

void bridge_handle_discord(const char* payload);
void bridge_handle_line(const char* line);
void bridge_handle_client(int client_fd);
void bridge_presence_playing(const char* version);

#ifdef _WIN32
DWORD WINAPI bridge_thread(LPVOID arg);
#else
void* bridge_thread(void* arg);
#endif

#endif
