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

int process_run(const char* exe, const char* const argv[]) {
    static char cmdline[131072];
    size_t len = 0;

    append_escaped(cmdline, sizeof(cmdline), &len, exe);
    for (int i = 1; argv[i] != NULL; i++) {
        if (len < sizeof(cmdline)) cmdline[len++] = ' ';
        append_escaped(cmdline, sizeof(cmdline), &len, argv[i]);
    }
    if (len < sizeof(cmdline)) cmdline[len] = '\0';
    else cmdline[sizeof(cmdline) - 1] = '\0';

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    DWORD flags = GetConsoleWindow() ? 0 : CREATE_NO_WINDOW;

    if (!CreateProcessA(exe, cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi)) {
        log_msg("error", "CreateProcess failed: %lu\n", GetLastError());
        return -1;
    }

    g_mc_process = pi.hProcess;

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    g_mc_process = NULL;

    return (int)code;
}

void process_kill(void) {
    if (g_mc_process) {
        TerminateProcess(g_mc_process, 0);
    }
}

#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

pid_t g_mc_process = -1;

int process_run(const char* exe, const char* const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        log_msg("error", "fork failed\n");
        return -1;
    }
    if (pid == 0) {
        execv(exe, (char* const*)argv);
        _exit(127);
    }

    g_mc_process = pid;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;

    g_mc_process = -1;

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

void process_kill(void) {
    if (g_mc_process > 0) {
        kill(g_mc_process, SIGTERM);
    }
}

#endif
