#include "bridge_internal.h"

void bridge_handle_client(int client_fd) {
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

        for (int i = 0; i < (int)n; i++) {
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
