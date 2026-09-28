#include "../../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <wininet.h>
  #include <shellapi.h>
  #pragma comment(lib, "wininet.lib")
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <sys/time.h>
  #include <sys/wait.h>
  #include <signal.h>
#endif

#include "../../include/ipc/system_handlers.h"
#include "../../include/ui/ui.h"
#include "../../include/core/logger.h"
#include "../../include/utils/sysmem.h"
#include <string.h>
#include <stdio.h>

static int check_internet(void) {
#ifdef _WIN32
    DWORD flags;
    if (!InternetGetConnectedState(&flags, 0)) return 0;
    HINTERNET hNet = InternetOpenA("CeroClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hNet) return 0;
    HINTERNET hUrl = InternetOpenUrlA(hNet, "https://www.google.com/generate_204",
                                      NULL, 0, INTERNET_FLAG_NO_CACHE_WRITE, 0);
    int ok = (hUrl != NULL);
    if (hUrl) InternetCloseHandle(hUrl);
    InternetCloseHandle(hNet);
    return ok;
#else
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo("www.google.com", "80", &hints, &res) != 0 || !res) return 0;
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return 0; }
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int ok = (connect(sock, res->ai_addr, res->ai_addrlen) == 0);
    close(sock);
    freeaddrinfo(res);
    return ok;
#endif
}

static int url_is_http_or_https(const char* url) {
    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

#ifndef _WIN32
static int try_exec_opener(const char* opener, const char* url) {
    char* const argv[] = { (char*)opener, (char*)url, NULL };
    execvp(opener, argv);
    return -1;
}

static void open_url_no_shell(const char* url) {
    pid_t pid = fork();
    if (pid < 0) return;

    if (pid == 0) {
        pid_t pid2 = fork();
        if (pid2 == 0) {
#ifdef __APPLE__
            try_exec_opener("/usr/bin/open", url);
#endif
            try_exec_opener("xdg-open", url);
            try_exec_opener("sensible-browser", url);
            try_exec_opener("x-www-browser", url);
            _exit(127);
        }
        _exit(0);
    }

    waitpid(pid, NULL, 0);
}
#endif

void on_shell_open(const char* id, const char* req, void* arg) {
    char url[2048] = "";
    const char* p = req;
    while (*p && *p != '"') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < sizeof(url))
            url[i++] = *p++;
        url[i] = '\0';
    }
    if (url[0] && url_is_http_or_https(url)) {
#ifdef _WIN32
        ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
        open_url_no_shell(url);
#endif
    } else if (url[0]) {
        log_msg("warn", "shell_open: URL refusee (schema non autorise): %s\n", url);
    }
    ui_return(arg, id, 0, "\"ok\"");
}

void on_check_internet(const char* id, const char* req, void* arg) {
    (void)req;
    ui_return(arg, id, 0, check_internet() ? "true" : "false");
}

void on_get_system_ram(const char* id, const char* req, void* arg) {
    (void)req;
    long total_mb = sysmem_total_mb();
    char out[32];
    snprintf(out, sizeof(out), "%ld", total_mb);
    ui_return(arg, id, 0, out);
}
