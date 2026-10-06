#include "../../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
#endif

#include "../../include/ipc/auth_handlers.h"
#include "../../include/platform/paths.h"
#include "../../include/discord/rpc_helpers.h"
#include "../../include/config/config.h"
#include "../../include/net/ms_auth.h"
#include "../../include/ui/ui.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  id[64];
    void* ui;
} AuthJob;

static AuthJob* auth_job_new(const char* id, void* ui) {
    AuthJob* j = (AuthJob*)calloc(1, sizeof(AuthJob));
    if (!j) return NULL;
    strncpy(j->id, id ? id : "", sizeof(j->id) - 1);
    j->ui = ui;
    return j;
}

#ifdef _WIN32
  typedef DWORD auth_thread_ret;
  #define AUTH_THREAD_CALL WINAPI
  #define AUTH_THREAD_RET  0
#else
  typedef void* auth_thread_ret;
  #define AUTH_THREAD_CALL
  #define AUTH_THREAD_RET  NULL
#endif

static int auth_spawn(auth_thread_ret (AUTH_THREAD_CALL *fn)(void*), AuthJob* j) {
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, fn, j, 0, NULL);
    if (!h) return -1;
    CloseHandle(h);
    return 0;
#else
    pthread_t t;
    if (pthread_create(&t, NULL, fn, j) != 0) return -1;
    pthread_detach(t);
    return 0;
#endif
}

static volatile long login_busy = 0;

static int login_try_lock(void) {
#ifdef _WIN32
    return InterlockedExchange(&login_busy, 1) == 0;
#else
    return __sync_lock_test_and_set(&login_busy, 1) == 0;
#endif
}

static void login_unlock(void) {
#ifdef _WIN32
    InterlockedExchange(&login_busy, 0);
#else
    __sync_lock_release(&login_busy);
#endif
}

static auth_thread_ret AUTH_THREAD_CALL check_account_thread(void* p) {
    AuthJob* j = (AuthJob*)p;

    char path[MAX_PATH_SIZE + 16];
    if (!build_account_path(path, sizeof(path))) {
        ui_return(j->ui, j->id, 1, "\"path_error\"");
        free(j);
        return AUTH_THREAD_RET;
    }

    int ok = 0;
    if (ms_auth_validate(path) == 0) {
        ok = 1;
    } else if (ms_auth_refresh(path) == 0) {
        ok = 1;
    }

    if (ok) rpc_set_idle();
    else    rpc_set_login();

    ui_return(j->ui, j->id, 0, ok ? "true" : "false");
    free(j);
    return AUTH_THREAD_RET;
}

void on_check_account(const char* id, const char* req, void* arg) {
    (void)req;

    AuthJob* j = auth_job_new(id, arg);
    if (!j) { ui_return(arg, id, 1, "\"alloc_error\""); return; }
    if (auth_spawn(check_account_thread, j) != 0) {
        free(j);
        ui_return(arg, id, 1, "\"thread_error\"");
    }
}

static auth_thread_ret AUTH_THREAD_CALL login_microsoft_thread(void* p) {
    AuthJob* j = (AuthJob*)p;

    char path[MAX_PATH_SIZE + 16];
    if (!build_account_path(path, sizeof(path))) {
        ui_return(j->ui, j->id, 1, "\"path_error\"");
        login_unlock();
        free(j);
        return AUTH_THREAD_RET;
    }

    int rc = ms_auth_login(path);
    if (rc == 0) {
        rpc_set_idle();
        ui_return(j->ui, j->id, 0, "\"ok\"");
    } else {
        char err[64];
        snprintf(err, sizeof(err), "\"error_%d\"", rc);
        ui_return(j->ui, j->id, 1, err);
    }

    login_unlock();
    free(j);
    return AUTH_THREAD_RET;
}

void on_login_microsoft(const char* id, const char* req, void* arg) {
    (void)req;

    if (!login_try_lock()) {
        ui_return(arg, id, 1, "\"login_in_progress\"");
        return;
    }

    AuthJob* j = auth_job_new(id, arg);
    if (!j) {
        login_unlock();
        ui_return(arg, id, 1, "\"alloc_error\"");
        return;
    }
    if (auth_spawn(login_microsoft_thread, j) != 0) {
        free(j);
        login_unlock();
        ui_return(arg, id, 1, "\"thread_error\"");
    }
}

void on_get_account(const char* id, const char* req, void* arg) {
    (void)req;

    char path[MAX_PATH_SIZE + 16];
    if (!build_account_path(path, sizeof(path))) {
        ui_return(arg, id, 1, "\"path_error\"");
        return;
    }

    FILE* f = fopen(path, "r");
    if (!f) {
        ui_return(arg, id, 1, "\"not_found\"");
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* buf = malloc(size + 1);
    if (!buf) { fclose(f); ui_return(arg, id, 1, "\"alloc_error\""); return; }

    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);

    ui_return(arg, id, 0, buf);
    free(buf);
}

static void get_mc_token_work(const char* id, void* arg) {

    char path[MAX_PATH_SIZE + 16];
    if (!build_account_path(path, sizeof(path))) {
        ui_return(arg, id, 1, "\"path_error\"");
        return;
    }

    log_msg("info", "Getting Minecraft Account Token...\n");

    if (ms_auth_validate(path) != 0) {
        if (ms_auth_refresh(path) != 0) {
            ui_return(arg, id, 1, "\"refresh_failed\"");
            log_msg("error", "Refresh Failed...\n");
            return;
        }
    }

    FILE* f = fopen(path, "r");
    if (!f) { ui_return(arg, id, 1, "\"not_found\""); log_msg("error", "Not found\n"); return; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0 || size > 1024 * 1024) { fclose(f); ui_return(arg, id, 1, "\"invalid_size\""); log_msg("error", "Invalid Size\n"); return; }

    char* buf = malloc(size + 1);
    if (!buf) { fclose(f); ui_return(arg, id, 1, "\"alloc_error\""); log_msg("error", "Alloc Error\n"); return; }
    size_t n = fread(buf, 1, size, f);
    buf[n] = '\0';
    fclose(f);

    const char* key = "\"mc_token\"";
    char* p = strstr(buf, key);
    if (!p) { free(buf); ui_return(arg, id, 1, "\"no_token\""); return; }
    p += strlen(key);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') { free(buf); ui_return(arg, id, 1, "\"parse_error\""); return; }
    p++;
    char* end = strchr(p, '"');
    if (!end) { free(buf); ui_return(arg, id, 1, "\"parse_error\""); return; }

    size_t tlen = (size_t)(end - p);
    char* out = malloc(tlen + 3);
    if (!out) { free(buf); ui_return(arg, id, 1, "\"alloc_error\""); return; }
    out[0] = '"';
    memcpy(out + 1, p, tlen);
    out[1 + tlen] = '"';
    out[2 + tlen] = '\0';

    ui_return(arg, id, 0, out);
    free(out);
    free(buf);
}

static auth_thread_ret AUTH_THREAD_CALL get_mc_token_thread(void* p) {
    AuthJob* j = (AuthJob*)p;
    get_mc_token_work(j->id, j->ui);
    free(j);
    return AUTH_THREAD_RET;
}

void on_get_mc_token(const char* id, const char* req, void* arg) {
    (void)req;

    AuthJob* j = auth_job_new(id, arg);
    if (!j) { ui_return(arg, id, 1, "\"alloc_error\""); return; }
    if (auth_spawn(get_mc_token_thread, j) != 0) {
        free(j);
        ui_return(arg, id, 1, "\"thread_error\"");
    }
}

void on_logout_account(const char* id, const char* req, void* arg) {
    (void)req;

    char path[MAX_PATH_SIZE + 16];
    if (!build_account_path(path, sizeof(path))) {
        ui_return(arg, id, 1, "\"path_error\"");
        return;
    }

    if (file_exists(path)) {
        if (remove(path) != 0) {
            log_msg("error", "logout_account: impossible de supprimer %s\n", path);
            ui_return(arg, id, 1, "\"remove_failed\"");
            return;
        }
    }

    log_msg("info", "Compte deconnecte (account.json supprime)\n");
    rpc_set_login();
    ui_return(arg, id, 0, "\"ok\"");
}