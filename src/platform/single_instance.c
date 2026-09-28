#include "../../include/platform/single_instance.h"

#ifdef _WIN32

#include <windows.h>

static HANDLE g_mutex = NULL;

int single_instance_check(void) {
    g_mutex = CreateMutexA(NULL, TRUE, "Global\\CeroClient_SingleInstance_Mutex");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowA(NULL, "CeroClient");
        if (other) {
            PostMessageA(other, CERO_MSG_SHOW, 0, 0);
        }
        if (g_mutex) { CloseHandle(g_mutex); g_mutex = NULL; }
        return 1;
    }
    return 0;
}

void single_instance_release(void) {
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}

void single_instance_write_port(int port) {
    (void)port;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int g_lock_fd = -1;
static char g_lock_path[512] = {0};

static void build_lock_path(char* out, size_t sz) {
    const char* runtime = getenv("XDG_RUNTIME_DIR");
#ifdef __APPLE__
    if (!runtime || !*runtime) runtime = getenv("TMPDIR"); /* per-user on macOS */
#endif
    if (!runtime || !*runtime) runtime = "/tmp";
    snprintf(out, sz, "%s/ceroclient.lock", runtime);
}

static int read_existing_port(void) {
    char path[512];
    build_lock_path(path, sizeof(path));

    FILE* f = fopen(path, "r");
    if (!f) return -1;

    int pid = -1, port = -1;
    if (fscanf(f, "%d %d", &pid, &port) != 2) port = -1;
    fclose(f);

    return port;
}

static void wake_existing_instance(void) {
    int port = read_existing_port();
    if (port <= 0) return;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        const char* msg = "SHOW\n";
        ssize_t w = write(sock, msg, strlen(msg));
        (void)w;
    }

    close(sock);
}

int single_instance_check(void) {
    build_lock_path(g_lock_path, sizeof(g_lock_path));

    g_lock_fd = open(g_lock_path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (g_lock_fd < 0) {
        return 0;
    }

    if (flock(g_lock_fd, LOCK_EX | LOCK_NB) != 0) {
        close(g_lock_fd);
        g_lock_fd = -1;

        wake_existing_instance();

        return 1;
    }

    char pid_str[32];
    int n = snprintf(pid_str, sizeof(pid_str), "%d\n", (int)getpid());
    if (ftruncate(g_lock_fd, 0) == 0) {
        ssize_t w = write(g_lock_fd, pid_str, (size_t)n);
        (void)w;
    }
    return 0;
}

void single_instance_write_port(int port) {
    if (g_lock_fd < 0) return;

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%d %d\n", (int)getpid(), port);

    if (ftruncate(g_lock_fd, 0) == 0) {
        lseek(g_lock_fd, 0, SEEK_SET);
        ssize_t w = write(g_lock_fd, buf, (size_t)n);
        (void)w;
    }
}

void single_instance_release(void) {
    if (g_lock_fd >= 0) {
        flock(g_lock_fd, LOCK_UN);
        close(g_lock_fd);
        g_lock_fd = -1;
        if (g_lock_path[0]) unlink(g_lock_path);
    }
}

#endif