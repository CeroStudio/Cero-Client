#include "../../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <pthread.h>
#endif

#include "../../include/ipc/bridge.h"
#include "../../include/launch/launch_minecraft.h"
#include "../../include/ipc/window_handlers.h"
#include "../../include/core/logger.h"
#include "../../include/discord/discord_rpc.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

int local_bridge_port = 0;

#define BRIDGE_LINE_MAX 512

static int g_game_fd = -1;

#ifdef _WIN32
static CRITICAL_SECTION g_game_lock;
static int g_game_lock_init = 0;
#else
static pthread_mutex_t g_game_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static void bridge_game_lock(void) {
#ifdef _WIN32
    if (g_game_lock_init) EnterCriticalSection(&g_game_lock);
#else
    pthread_mutex_lock(&g_game_lock);
#endif
}

static void bridge_game_unlock(void) {
#ifdef _WIN32
    if (g_game_lock_init) LeaveCriticalSection(&g_game_lock);
#else
    pthread_mutex_unlock(&g_game_lock);
#endif
}

void bridge_send_to_game(const char* message) {
    if (message == NULL) return;

    bridge_game_lock();
    int fd = g_game_fd;
    bridge_game_unlock();

    if (fd < 0) {
        log_msg("debug", "[Bridge] Aucun jeu connecté, message ignoré : %s\n", message);
        return;
    }

    size_t len = strlen(message);
    const char nl = '\n';
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int w = send(fd, message + sent, (int)(len - sent), 0);
#else
        ssize_t w = send(fd, message + sent, len - sent, 0);
#endif
        if (w <= 0) {
            log_msg("warn", "[Bridge] Envoi vers le jeu échoué\n");
            return;
        }
        sent += (size_t)w;
    }
#ifdef _WIN32
    send(fd, &nl, 1, 0);
#else
    (void)send(fd, &nl, 1, 0);
#endif
}

static void bridge_handle_discord(const char* payload) {
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

    discord_rpc_update(state, details, "logo", "CeroClient", NULL, NULL, ts);
}

static void bridge_handle_line(const char* line) {
    if (line == NULL || line[0] == '\0') return;

    log_msg("debug", "[Bridge] <- jeu : %s\n", line);

    if (strncmp(line, "SHOW", 4) == 0 && (line[4] == '\0' || line[4] == ';')) {
        show_main_window();
    } else if (strncmp(line, "DISCORD_CLEAR", 13) == 0 && line[13] == '\0') {
        discord_rpc_clear();
    } else if (strncmp(line, "DISCORD;", 8) == 0) {
        bridge_handle_discord(line + 8);
    } else if (strncmp(line, "HELLO;", 6) == 0) {
        log_msg("info", "[Bridge] Jeu connecté : %s\n", line + 6);
    } else {
        log_msg("warn", "[Bridge] Message inconnu : %s\n", line);
    }
}

static void bridge_handle_client(int client_fd) {
    char recv_buf[512];
    char line[BRIDGE_LINE_MAX];
    size_t line_len = 0;

    bridge_game_lock();
    g_game_fd = client_fd;
    bridge_game_unlock();

    for (;;) {
#ifdef _WIN32
        int n = recv(client_fd, recv_buf, (int)sizeof(recv_buf), 0);
#else
        ssize_t n = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
#endif
        if (n <= 0) break;

        for (ssize_t i = 0; i < n; i++) {
            char ch = recv_buf[i];
            if (ch == '\r') continue;
            if (ch == '\n') {
                line[line_len] = '\0';
                if (line_len > 0) bridge_handle_line(line);
                line_len = 0;
            } else if (line_len < sizeof(line) - 1) {
                line[line_len++] = ch;
            }
        }
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        bridge_handle_line(line);
    }

    bridge_game_lock();
    if (g_game_fd == client_fd) g_game_fd = -1;
    bridge_game_unlock();
}

#ifdef _WIN32
static DWORD WINAPI bridge_thread(LPVOID arg) {
    (void)arg;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) return 1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(0);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    int len = (int)sizeof(addr);
    getsockname(server_fd, (struct sockaddr*)&addr, &len);
    local_bridge_port = ntohs(addr.sin_port);

    log_msg("info", "Local TCP bridge listening on the port %d\n", local_bridge_port);

    while (1) {
        SOCKET client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == INVALID_SOCKET) continue;

        log_msg("info", "[Bridge] Session de jeu ouverte\n");
        bridge_handle_client((int)client_fd);
        closesocket(client_fd);
        log_msg("info", "[Bridge] Session de jeu fermée\n");
    }
    return 0;
}

void bridge_start(void) {
    InitializeCriticalSection(&g_game_lock);
    g_game_lock_init = 1;
    CreateThread(NULL, 0, bridge_thread, NULL, 0, NULL);
    Sleep(100);
}

#else

static void* bridge_thread(void* arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_LOOPBACK;
    addr.sin_port = htons(0);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    socklen_t len = sizeof(addr);
    getsockname(server_fd, (struct sockaddr*)&addr, &len);
    local_bridge_port = ntohs(addr.sin_port);

    log_msg("info", "Local TCP bridge listening on the port %d\n", local_bridge_port);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        log_msg("info", "[Bridge] Session de jeu ouverte\n");
        bridge_handle_client(client_fd);
        close(client_fd);
        log_msg("info", "[Bridge] Session de jeu fermée\n");
    }
    return NULL;
}

void bridge_start(void) {
    pthread_t bridge_tid;
    pthread_create(&bridge_tid, NULL, bridge_thread, NULL);
    pthread_detach(bridge_tid);
    usleep(100000);
}

#endif