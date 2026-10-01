#include "bridge_internal.h"
#include "../../../include/core/logger.h"
#include <string.h>

#ifdef _WIN32
DWORD WINAPI bridge_thread(LPVOID arg) {
    (void)arg;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_fd == INVALID_SOCKET) return 1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
#else
void* bridge_thread(void* arg) {
    (void)arg;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return NULL;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
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
#endif
