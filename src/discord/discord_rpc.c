#include "../../include/discord/discord_rpc.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #include <windows.h>
  static HANDLE g_pipe = INVALID_HANDLE_VALUE;
  static SRWLOCK g_rpc_lock = SRWLOCK_INIT;
  #define RPC_LOCK()   AcquireSRWLockExclusive(&g_rpc_lock)
  #define RPC_UNLOCK() ReleaseSRWLockExclusive(&g_rpc_lock)
  #define RPC_VALID() (g_pipe != INVALID_HANDLE_VALUE)
#else
  #include <unistd.h>
  #include <pthread.h>
  #include <sys/socket.h>
  #include <sys/un.h>
  static int g_sock = -1;
  static pthread_mutex_t g_rpc_lock = PTHREAD_MUTEX_INITIALIZER;
  #define RPC_LOCK()   pthread_mutex_lock(&g_rpc_lock)
  #define RPC_UNLOCK() pthread_mutex_unlock(&g_rpc_lock)
  #define RPC_VALID() (g_sock >= 0)
#endif

static char g_client_id[64] = {0};
static int  g_pid = 0;

static int rpc_write_raw(const void* data, size_t len) {
#ifdef _WIN32
    DWORD written;
    return WriteFile(g_pipe, data, (DWORD)len, &written, NULL) ? 0 : -1;
#else
    return (write(g_sock, data, len) == (ssize_t)len) ? 0 : -1;
#endif
}

static int rpc_send(uint32_t opcode, const char* payload) {
    uint32_t len = (uint32_t)strlen(payload);
    uint32_t header[2] = { opcode, len };
    if (rpc_write_raw(header, sizeof(header)) < 0) return -1;
    if (rpc_write_raw(payload, len) < 0) return -1;
    return 0;
}

static int rpc_read_response(void) {
#ifdef _WIN32
    DWORD waited = 0;
    DWORD available = 0;
    while (waited < 3000) {
        if (!PeekNamedPipe(g_pipe, NULL, 0, NULL, &available, NULL)) {
            log_msg("error", "[RPC] PeekNamedPipe failed\n");
            return -1;
        }
        if (available >= 8) break;
        Sleep(50);
        waited += 50;
    }
    if (available < 8) {
        log_msg("warn", "[RPC] Read timeout (Discord not responding)\n");
        return -1;
    }

    uint32_t header[2] = {0};
    DWORD read = 0;
    if (!ReadFile(g_pipe, header, sizeof(header), &read, NULL) || read != 8) return -1;

    if (header[1] > 0 && header[1] < 4096) {
        char buf[4096];
        DWORD total = 0;
        while (total < header[1]) {
            DWORD r = 0;
            if (!ReadFile(g_pipe, buf + total, header[1] - total, &r, NULL) || r == 0) return -1;
            total += r;
        }
        buf[header[1]] = 0;
        log_msg("debug", "[RPC] Response: %s\n", buf);
    }
#else
#endif
    return 0;
}

#ifdef _WIN32
static int get_pid(void) { return (int)GetCurrentProcessId(); }
#else
static int get_pid(void) { return (int)getpid(); }
#endif

int discord_rpc_init(const char* client_id) {
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(g_client_id, client_id, sizeof(g_client_id) - 1);
    g_pid = get_pid();

#ifdef _WIN32
    for (int i = 0; i < 10; i++) {
        char name[64];
        snprintf(name, sizeof(name), "\\\\?\\pipe\\discord-ipc-%d", i);
        g_pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, 0, NULL);
        if (g_pipe != INVALID_HANDLE_VALUE) {
            log_msg("info", "[RPC] Pipe opened: %s\n", name);
            break;
        }
    }

    if (g_pipe == INVALID_HANDLE_VALUE) {
        log_msg("warn", "[RPC] No Discord pipe found\n");
        return -1;
    }
#else
    const char* tmp = getenv("XDG_RUNTIME_DIR");
    if (!tmp) tmp = getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";

    for (int i = 0; i < 10; i++) {
        struct sockaddr_un addr = {0};
        addr.sun_family = AF_UNIX;
        snprintf(addr.sun_path, sizeof(addr.sun_path),
                 "%s/discord-ipc-%d", tmp, i);

        g_sock = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_sock < 0) continue;

        if (connect(g_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            log_msg("info", "[RPC] Socket connected: %s\n", addr.sun_path);
            break;
        }
        close(g_sock);
        g_sock = -1;
    }
    if (g_sock < 0) {
        log_msg("warn", "[RPC] No Discord socket found\n");
        return -1;
    }
#endif

    char hs[256];
    snprintf(hs, sizeof(hs),
             "{\"v\":1,\"client_id\":\"%s\"}", g_client_id);
    if (rpc_send(0, hs) < 0) {
        log_msg("error", "[RPC] Handshake send failed\n");
        discord_rpc_shutdown();
        return -1;
    }
    if (rpc_read_response() < 0) {
        log_msg("error", "[RPC] Handshake response failed\n");
        discord_rpc_shutdown();
        return -1;
    }
    log_msg("succes", "[RPC] Handshake OK\n");
    return 0;
}

static void json_escape(char* dst, size_t dsz, const char* src) {
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j < dsz - 2; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j < dsz - 3) dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = 0;
}

void discord_rpc_update(const char* state, const char* details,
                        const char* large_image, const char* large_text,
                        const char* small_image, const char* small_text,
                        int64_t start_timestamp) {
    if (!RPC_VALID()) return;

    RPC_LOCK();

    char payload[2048];
    char buf_state[256]   = "";
    char buf_details[256] = "";
    char buf_li[512]      = "";
    char buf_lt[256]      = "";
    char buf_si[512]      = "";
    char buf_st[256]      = "";

    if (state)       json_escape(buf_state,   sizeof(buf_state),   state);
    if (details)     json_escape(buf_details, sizeof(buf_details), details);
    if (large_image) json_escape(buf_li,      sizeof(buf_li),      large_image);
    if (large_text)  json_escape(buf_lt,      sizeof(buf_lt),      large_text);
    if (small_image) json_escape(buf_si,      sizeof(buf_si),      small_image);
    if (small_text)  json_escape(buf_st,      sizeof(buf_st),      small_text);

    char activity[1536];
    int off = snprintf(activity, sizeof(activity), "{");

    if (state)   off += snprintf(activity+off, sizeof(activity)-off,
                  "\"state\":\"%s\",", buf_state);
    if (details) off += snprintf(activity+off, sizeof(activity)-off,
                  "\"details\":\"%s\",", buf_details);
    if (start_timestamp > 0)
        off += snprintf(activity+off, sizeof(activity)-off,
                  "\"timestamps\":{\"start\":%lld},", (long long)start_timestamp);

    if (large_image || small_image) {
        off += snprintf(activity+off, sizeof(activity)-off, "\"assets\":{");
        int first = 1;
        if (large_image) {
            off += snprintf(activity+off, sizeof(activity)-off,
                  "\"large_image\":\"%s\"", buf_li);
            first = 0;
            if (large_text)
                off += snprintf(activity+off, sizeof(activity)-off,
                      ",\"large_text\":\"%s\"", buf_lt);
        }
        if (small_image) {
            if (!first) off += snprintf(activity+off, sizeof(activity)-off, ",");
            off += snprintf(activity+off, sizeof(activity)-off,
                  "\"small_image\":\"%s\"", buf_si);
            if (small_text)
                off += snprintf(activity+off, sizeof(activity)-off,
                      ",\"small_text\":\"%s\"", buf_st);
        }
        off += snprintf(activity+off, sizeof(activity)-off, "},");
    }

    if (off > 1 && activity[off-1] == ',') activity[--off] = 0;
    snprintf(activity+off, sizeof(activity)-off, "}");

    snprintf(payload, sizeof(payload),
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d,\"activity\":%s},\"nonce\":\"1\"}",
        g_pid, activity);

    if (rpc_send(1, payload) == 0) {
        rpc_read_response();
        log_msg("info", "[RPC] Activity updated\n");
    } else {
        log_msg("error", "[RPC] Activity send failed\n");
    }

    RPC_UNLOCK();
}

void discord_rpc_clear(void) {
    if (!RPC_VALID()) return;

    RPC_LOCK();

    char payload[128];
    snprintf(payload, sizeof(payload),
        "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":%d},\"nonce\":\"1\"}", g_pid);
    rpc_send(1, payload);
    rpc_read_response();
    log_msg("info", "[RPC] Activity cleared\n");

    RPC_UNLOCK();
}

void discord_rpc_shutdown(void) {
#ifdef _WIN32
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }
#endif
    log_msg("info", "[RPC] Shutdown\n");
}