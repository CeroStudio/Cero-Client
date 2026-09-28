#define CURL_STATICLIB

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <curl/curl.h>
#include "../../include/net/ca_bundle.h"
#include "../../include/core/logger.h"

#ifndef _WIN32
  #include <unistd.h>
  #include <libgen.h>
#endif

#define CACERT_URL "https://curl.se/ca/cacert.pem"

static char g_ca_path[1024] = {0};

static int resolve_path(void) {
    if (g_ca_path[0]) return 1;
#ifdef __APPLE__
    return 0;
#endif

    char exe[1024];

#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) return 0;
    char* sep = strrchr(exe, '\\');
    if (!sep) return 0;
    *sep = '\0';
    snprintf(g_ca_path, sizeof(g_ca_path), "%s\\cacert.pem", exe);
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return 0;
    exe[n] = '\0';
    snprintf(g_ca_path, sizeof(g_ca_path), "%s/cacert.pem", dirname(exe));
#endif

    return 1;
}

static size_t write_cb(void* ptr, size_t sz, size_t nm, void* ud) {
    return fwrite(ptr, sz, nm, (FILE*)ud);
}

const char* ca_bundle_path(void) {
    return resolve_path() ? g_ca_path : NULL;
}

int ca_bundle_ensure(void) {
#ifdef __APPLE__
    /* No /proc/self/exe on macOS; libcurl uses the system trust store when
     * ca_bundle_path() is NULL, so there is nothing to download. */
    return 1;
#endif
    if (!resolve_path()) {
        log_msg("erreur", "Impossible de resoudre le chemin de l'executable\n");
        return 0;
    }

    FILE* f = fopen(g_ca_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size > 100000) {
            log_msg("info", "cacert.pem found (%ld octets)\n", size);
            return 1;
        }
        log_msg("warn", "cacert.pem invalid, redownloading....\n");
    }

    log_msg("info", "Downloading cacert.pem...\n");

    char tmp[1088];
    snprintf(tmp, sizeof(tmp), "%s.tmp", g_ca_path);

    FILE* out = fopen(tmp, "wb");
    if (!out) {
        log_msg("erreur", "Cannot write %s\n", tmp);
        return 0;
    }

    CURL* e = curl_easy_init();
    if (!e) { fclose(out); remove(tmp); return 0; }

    curl_easy_setopt(e, CURLOPT_URL, CACERT_URL);
    curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(e, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(e, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);

#ifdef _WIN32
    curl_easy_setopt(e, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif

    CURLcode res = curl_easy_perform(e);
    long code = 0;
    curl_easy_getinfo(e, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(e);
    fclose(out);

    if (res != CURLE_OK || code != 200) {
        log_msg("erreur", "Failed while downloading cacert.pem (curl=%d http=%ld): %s\n",
                res, code, curl_easy_strerror(res));
        remove(tmp);
        return 0;
    }

    remove(g_ca_path);
    if (rename(tmp, g_ca_path) != 0) {
        log_msg("erreur", "Cannot rename %s\n", tmp);
        remove(tmp);
        return 0;
    }

    log_msg("succes", "cacert.pem install: %s\n", g_ca_path);
    return 1;
}