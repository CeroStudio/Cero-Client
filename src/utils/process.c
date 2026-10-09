#include "../../include/utils/process.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

HANDLE g_mc_process = NULL;

static void append_escaped(char* dst, size_t cap, size_t* len, const char* arg) {
    int needs_quotes = (*arg == '\0') || strpbrk(arg, " \t\"") != NULL;

    if (needs_quotes && *len < cap) dst[(*len)++] = '"';

    for (const char* p = arg; *p; p++) {
        int backslashes = 0;
        while (*p == '\\') { backslashes++; p++; }

        if (*p == '\0') {
            for (int i = 0; i < backslashes * (needs_quotes ? 2 : 1); i++)
                if (*len < cap) dst[(*len)++] = '\\';
            break;
        } else if (*p == '"') {
            for (int i = 0; i < backslashes * 2 + 1; i++)
                if (*len < cap) dst[(*len)++] = '\\';
            if (*len < cap) dst[(*len)++] = '"';
        } else {
            for (int i = 0; i < backslashes; i++)
                if (*len < cap) dst[(*len)++] = '\\';
            if (*len < cap) dst[(*len)++] = *p;
        }
    }

    if (needs_quotes && *len < cap) dst[(*len)++] = '"';
}

int process_spawn(const char* exe, const char* const argv[], process_handle_t* out) {
    const size_t cap = 131072;
    char* cmdline = (char*)malloc(cap);
    if (!cmdline) return -1;
    size_t len = 0;

    append_escaped(cmdline, cap, &len, exe);
    for (int i = 1; argv[i] != NULL; i++) {
        if (len < cap) cmdline[len++] = ' ';
        append_escaped(cmdline, cap, &len, argv[i]);
    }
    if (len < cap) cmdline[len] = '\0';
    else cmdline[cap - 1] = '\0';

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    DWORD flags = GetConsoleWindow() ? 0 : CREATE_NO_WINDOW;

    BOOL ok = CreateProcessA(exe, cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {
        log_msg("error", "CreateProcess failed: %lu\n", GetLastError());
        return -1;
    }

    CloseHandle(pi.hThread);
    *out = pi.hProcess;
    return 0;
}

int process_wait(process_handle_t h) {
    WaitForSingleObject(h, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    return (int)code;
}

void process_close(process_handle_t h) {
    if (h) CloseHandle(h);
}

void process_terminate(process_handle_t h) {
    if (h) TerminateProcess(h, 0);
}

int process_run(const char* exe, const char* const argv[]) {
    process_handle_t h;
    if (process_spawn(exe, argv, &h) != 0) return -1;

    g_mc_process = h;
    int code = process_wait(h);
    g_mc_process = NULL;
    process_close(h);
    return code;
}

void process_kill(void) {
    HANDLE h = g_mc_process;
    if (h) TerminateProcess(h, 0);
}

#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

pid_t g_mc_process = -1;

int process_spawn(const char* exe, const char* const argv[], process_handle_t* out) {
    pid_t pid = fork();
    if (pid < 0) {
        log_msg("error", "fork failed\n");
        return -1;
    }
    if (pid == 0) {
        execv(exe, (char* const*)argv);
        _exit(127);
    }

    *out = pid;
    return 0;
}

int process_wait(process_handle_t h) {
#ifdef WNOWAIT
    siginfo_t info;
    int r;
    memset(&info, 0, sizeof(info));
    do {
        r = waitid(P_PID, (id_t)h, &info, WEXITED | WNOWAIT);
    } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;

    if (info.si_code == CLD_EXITED) return info.si_status;
    return -1;
#else
    int status = 0;
    pid_t r;
    do {
        r = waitpid(h, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) return -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
#endif
}

void process_close(process_handle_t h) {
#ifdef WNOWAIT
    int status = 0;
    while (waitpid(h, &status, 0) < 0 && errno == EINTR) { }
#else
    (void)h;
#endif
}

void process_terminate(process_handle_t h) {
    if (h > 0) kill(h, SIGTERM);
}

int process_run(const char* exe, const char* const argv[]) {
    process_handle_t h;
    if (process_spawn(exe, argv, &h) != 0) return -1;

    g_mc_process = h;
    int code = process_wait(h);
    g_mc_process = -1;
    return code;
}

void process_kill(void) {
    pid_t p = g_mc_process;
    if (p > 0) {
        kill(p, SIGTERM);
    }
}

#endif