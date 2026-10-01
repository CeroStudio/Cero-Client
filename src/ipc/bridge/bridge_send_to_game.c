#include "bridge_internal.h"
#include "../../../include/ipc/bridge.h"
#include "../../../include/core/logger.h"
#include <string.h>

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
