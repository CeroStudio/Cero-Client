#include "../../include/net/ms_auth.h"
#include "../../include/core/logger.h"
#include "../../include/vendor/cJSON.h"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <wininet.h>
  #include <shellapi.h>
  #include <wchar.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <sys/types.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <curl/curl.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <libgen.h>
#endif

#define AZURE_CLIENT_ID "29664b39-573c-4d3b-a031-4102f3b9e3aa"
#define REDIRECT_PORT   43219
#define REDIRECT_URI    "http://127.0.0.1:43219/"
#define MC_TOKEN_MAX_AGE_SECONDS (23 * 3600)

static void mkdir_parent(const char* path) {
#ifdef _WIN32

#else
    char tmp[4096];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = '\0';
    char* dir = dirname(tmp);
    mkdir(dir, 0755);
#endif
}

static int write_file(const char* path, const char* data) {
    mkdir_parent(path);
    FILE* f = fopen(path, "wb");
    if (!f) {
        log_msg("error", "write_file: impossible d'écrire dans %s\n", path);
        return -1;
    }
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return 0;
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static char* wait_for_oauth_code(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return NULL;
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { WSACleanup(); return NULL; }
#else
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return NULL;
#endif

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(REDIRECT_PORT);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(srv, 1) != 0) {
#ifdef _WIN32
        closesocket(srv); WSACleanup();
#else
        close(srv);
#endif
        return NULL;
    }

#ifdef _WIN32
    SOCKET cli = accept(srv, NULL, NULL);
    if (cli == INVALID_SOCKET) { closesocket(srv); WSACleanup(); return NULL; }
#else
    int cli = accept(srv, NULL, NULL);
    if (cli < 0) { close(srv); return NULL; }
#endif

    char buf[4096] = {0};
    int n = recv(cli, buf, sizeof(buf)-1, 0);
    (void)n;

    char* code = NULL;
    char* p = strstr(buf, "code=");
    if (p) {
        p += 5;
        char* end = p;
        while (*end && *end != '&' && *end != ' ' && *end != '\r') end++;
        size_t len = end - p;
        code = (char*)malloc(len + 1);
        memcpy(code, p, len);
        code[len] = 0;
    }

    const char* html =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n\r\n"
        "<script>window.location.href=\"https://cerostudio.fr/ceroclient/login/succes.html\"</script>";
    send(cli, html, (int)strlen(html), 0);

#ifdef _WIN32
    closesocket(cli);
    closesocket(srv);
    WSACleanup();
#else
    close(cli);
    close(srv);
#endif
    return code;
}

#ifdef _WIN32
static char* http_post(const wchar_t* host, INTERNET_PORT port, const wchar_t* path,
                       int use_https, const char* content_type,
                       const char* body, size_t body_len, int* http_status)
{
    if (http_status) *http_status = 0;

    HINTERNET hInet = InternetOpenW(L"CeroClient/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return NULL;

    HINTERNET hCon = InternetConnectW(hInet, host, port,
        NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hCon) { InternetCloseHandle(hInet); return NULL; }

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
    if (use_https) flags |= INTERNET_FLAG_SECURE;

    const wchar_t* accept_types[] = { L"*/*", NULL };
    HINTERNET hReq = HttpOpenRequestW(hCon, L"POST", path, NULL, NULL,
        accept_types, flags, 0);
    if (!hReq) { InternetCloseHandle(hCon); InternetCloseHandle(hInet); return NULL; }

    wchar_t header[256];
    if (content_type && *content_type) {
        wchar_t ct_w[128];
        MultiByteToWideChar(CP_UTF8, 0, content_type, -1, ct_w, 128);
        swprintf(header, 256, L"Content-Type: %ls\r\n", ct_w);
    } else {
        header[0] = 0;
    }

    BOOL ok = HttpSendRequestW(hReq,
        header[0] ? header : NULL,
        header[0] ? (DWORD)-1 : 0,
        (LPVOID)body, (DWORD)body_len);

    if (!ok) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hCon);
        InternetCloseHandle(hInet);
        return NULL;
    }

    DWORD status = 0, slen = sizeof(status);
    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &status, &slen, NULL);
    if (http_status) *http_status = (int)status;

    size_t cap = 8192, sz = 0;
    char* out = (char*)malloc(cap);
    if (!out) {
        InternetCloseHandle(hReq);
        InternetCloseHandle(hCon);
        InternetCloseHandle(hInet);
        return NULL;
    }

    for (;;) {
        DWORD avail = 0;
        if (!InternetQueryDataAvailable(hReq, &avail, 0, 0)) break;
        if (avail == 0) break;
        if (sz + avail + 1 > cap) {
            while (sz + avail + 1 > cap) cap *= 2;
            char* n = (char*)realloc(out, cap);
            if (!n) { free(out); out = NULL; break; }
            out = n;
        }
        DWORD rd = 0;
        if (!InternetReadFile(hReq, out + sz, avail, &rd) || rd == 0) break;
        sz += rd;
    }
    if (out) out[sz] = 0;

    InternetCloseHandle(hReq);
    InternetCloseHandle(hCon);
    InternetCloseHandle(hInet);
    return out;
}

static char* http_post_url(const char* host, const char* path,
                           const char* content_type,
                           const char* body, size_t body_len,
                           int* http_status) {
    wchar_t host_w[256], path_w[512];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, host_w, 256);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, path_w, 512);
    return http_post(host_w, INTERNET_DEFAULT_HTTPS_PORT, path_w,
                     1, content_type, body, body_len, http_status);
}

static char* http_get_bearer(const char* host, const char* path,
                             const char* bearer_token, int* http_status) {
    if (http_status) *http_status = 0;

    HINTERNET hInet = InternetOpenW(L"CeroClient/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return NULL;

    wchar_t host_w[256], path_w[512];
    MultiByteToWideChar(CP_UTF8, 0, host, -1, host_w, 256);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, path_w, 512);

    HINTERNET hCon = InternetConnectW(hInet, host_w,
        INTERNET_DEFAULT_HTTPS_PORT, NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);
    if (!hCon) { InternetCloseHandle(hInet); return NULL; }

    const wchar_t* accept_types[] = { L"application/json", NULL };
    HINTERNET hReq = HttpOpenRequestW(hCon, L"GET",
        path_w, NULL, NULL, accept_types,
        INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
    if (!hReq) { InternetCloseHandle(hCon); InternetCloseHandle(hInet); return NULL; }

    wchar_t auth[2048];
    wchar_t tok_w[1600];
    MultiByteToWideChar(CP_UTF8, 0, bearer_token, -1, tok_w, 1600);
    swprintf(auth, 2048, L"Authorization: Bearer %ls\r\n", tok_w);

    BOOL ok = HttpSendRequestW(hReq, auth, (DWORD)-1, NULL, 0);
    if (!ok) {
        InternetCloseHandle(hReq); InternetCloseHandle(hCon); InternetCloseHandle(hInet);
        return NULL;
    }

    DWORD status = 0, slen = sizeof(status);
    HttpQueryInfoW(hReq, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                   &status, &slen, NULL);
    if (http_status) *http_status = (int)status;

    size_t cap = 4096, sz = 0;
    char* out = (char*)malloc(cap);
    if (!out) { InternetCloseHandle(hReq); InternetCloseHandle(hCon); InternetCloseHandle(hInet); return NULL; }

    for (;;) {
        DWORD avail = 0;
        if (!InternetQueryDataAvailable(hReq, &avail, 0, 0)) break;
        if (avail == 0) break;
        if (sz + avail + 1 > cap) {
            while (sz + avail + 1 > cap) cap *= 2;
            char* n = (char*)realloc(out, cap);
            if (!n) { free(out); out = NULL; break; }
            out = n;
        }
        DWORD rd = 0;
        if (!InternetReadFile(hReq, out + sz, avail, &rd) || rd == 0) break;
        sz += rd;
    }
    if (out) out[sz] = 0;

    InternetCloseHandle(hReq);
    InternetCloseHandle(hCon);
    InternetCloseHandle(hInet);
    return out;
}

#else

typedef struct {
    char*  data;
    size_t size;
    size_t cap;
} curl_buf_t;

static size_t curl_write_cb(void* ptr, size_t sz, size_t nmemb, void* ud) {
    curl_buf_t* b = (curl_buf_t*)ud;
    size_t add = sz * nmemb;
    if (b->size + add + 1 > b->cap) {
        size_t newcap = b->cap ? b->cap : 8192;
        while (newcap < b->size + add + 1) newcap *= 2;
        char* n = (char*)realloc(b->data, newcap);
        if (!n) return 0;
        b->data = n;
        b->cap = newcap;
    }
    memcpy(b->data + b->size, ptr, add);
    b->size += add;
    b->data[b->size] = 0;
    return add;
}

static char* http_post_url(const char* host, const char* path,
                           const char* content_type,
                           const char* body, size_t body_len,
                           int* http_status) {
    if (http_status) *http_status = 0;

    CURL* c = curl_easy_init();
    if (!c) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "https://%s%s", host, path);

    curl_buf_t buf = {0};

    struct curl_slist* headers = NULL;
    if (content_type && *content_type) {
        char h[256];
        snprintf(h, sizeof(h), "Content-Type: %s", content_type);
        headers = curl_slist_append(headers, h);
    }
    headers = curl_slist_append(headers, "User-Agent: CeroClient/1.0");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body_len);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (http_status) *http_status = (int)code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        log_msg("error", "curl POST %s failed: %s\n", url, curl_easy_strerror(rc));
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

static char* http_get_bearer(const char* host, const char* path,
                             const char* bearer_token, int* http_status) {
    if (http_status) *http_status = 0;

    CURL* c = curl_easy_init();
    if (!c) return NULL;

    char url[1024];
    snprintf(url, sizeof(url), "https://%s%s", host, path);

    curl_buf_t buf = {0};

    char auth[2200];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", bearer_token);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "User-Agent: CeroClient/1.0");

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (http_status) *http_status = (int)code;

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        log_msg("error", "curl GET %s failed: %s\n", url, curl_easy_strerror(rc));
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

#endif

static void open_browser(const char* url) {
#ifdef _WIN32
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
    char cmd[2048];
    
#ifdef __APPLE__
    snprintf(cmd, sizeof(cmd), "open '%s' >/dev/null 2>&1 &", url);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' >/dev/null 2>&1 &", url);
#endif
    int rc = system(cmd);
    (void)rc;
#endif
}

static int exchange_code_for_token(const char* code, char** access, char** refresh) {
    char body[2048];
    snprintf(body, sizeof(body),
        "client_id=%s"
        "&code=%s"
        "&grant_type=authorization_code"
        "&redirect_uri=" REDIRECT_URI
        "&scope=XboxLive.signin%%20offline_access",
        AZURE_CLIENT_ID, code);

    int status = 0;
    char* resp = http_post_url("login.microsoftonline.com",
        "/consumers/oauth2/v2.0/token",
        "application/x-www-form-urlencoded", body, strlen(body), &status);
    if (!resp) return -1;

    cJSON* j = cJSON_Parse(resp);
    free(resp);
    if (!j) return -2;

    cJSON* at = cJSON_GetObjectItem(j, "access_token");
    cJSON* rt = cJSON_GetObjectItem(j, "refresh_token");
    if (!at || !rt) { cJSON_Delete(j); return -3; }

    *access  = strdup(at->valuestring);
    *refresh = strdup(rt->valuestring);
    cJSON_Delete(j);
    return 0;
}

static int xbox_auth(const char* ms_token, char** xbl_token, char** userhash) {
    char body[4096];
    snprintf(body, sizeof(body),
        "{\"Properties\":{\"AuthMethod\":\"RPS\",\"SiteName\":\"user.auth.xboxlive.com\","
        "\"RpsTicket\":\"d=%s\"},"
        "\"RelyingParty\":\"http://auth.xboxlive.com\",\"TokenType\":\"JWT\"}",
        ms_token);

    int status = 0;
    char* resp = http_post_url("user.auth.xboxlive.com",
        "/user/authenticate",
        "application/json", body, strlen(body), &status);
    if (!resp) return -1;

    cJSON* j = cJSON_Parse(resp); free(resp);
    if (!j) return -2;

    cJSON* tok = cJSON_GetObjectItem(j, "Token");
    cJSON* dc  = cJSON_GetObjectItem(j, "DisplayClaims");
    if (!tok || !dc) { cJSON_Delete(j); return -3; }
    cJSON* xui = cJSON_GetObjectItem(dc, "xui");
    cJSON* uh  = xui ? cJSON_GetObjectItem(cJSON_GetArrayItem(xui, 0), "uhs") : NULL;
    if (!uh) { cJSON_Delete(j); return -4; }

    *xbl_token = strdup(tok->valuestring);
    *userhash  = strdup(uh->valuestring);
    cJSON_Delete(j);
    return 0;
}

static int xsts_auth(const char* xbl, char** xsts) {
    char body[4096];
    snprintf(body, sizeof(body),
        "{\"Properties\":{\"SandboxId\":\"RETAIL\",\"UserTokens\":[\"%s\"]},"
        "\"RelyingParty\":\"rp://api.minecraftservices.com/\",\"TokenType\":\"JWT\"}",
        xbl);

    int status = 0;
    char* resp = http_post_url("xsts.auth.xboxlive.com",
        "/xsts/authorize",
        "application/json", body, strlen(body), &status);
    if (!resp) return -1;

    cJSON* j = cJSON_Parse(resp); free(resp);
    if (!j) return -2;
    cJSON* tok = cJSON_GetObjectItem(j, "Token");
    if (!tok) { cJSON_Delete(j); return -3; }
    *xsts = strdup(tok->valuestring);
    cJSON_Delete(j);
    return 0;
}

static int mc_auth(const char* xsts, const char* uhs, char** mc_token) {
    char body[4096];
    snprintf(body, sizeof(body),
        "{\"identityToken\":\"XBL3.0 x=%s;%s\"}", uhs, xsts);

    int status = 0;
    char* resp = http_post_url("api.minecraftservices.com",
        "/authentication/login_with_xbox",
        "application/json", body, strlen(body), &status);
    if (!resp) return -1;

    cJSON* j = cJSON_Parse(resp); free(resp);
    if (!j) return -2;
    cJSON* tok = cJSON_GetObjectItem(j, "access_token");
    if (!tok) { cJSON_Delete(j); return -3; }
    *mc_token = strdup(tok->valuestring);
    cJSON_Delete(j);
    return 0;
}

static char* mc_profile(const char* access_token, int* http_status) {
    return http_get_bearer("api.minecraftservices.com",
                           "/minecraft/profile",
                           access_token, http_status);
}

int ms_auth_validate(const char* save_path) {
    char* data = read_file(save_path);
    if (!data) return -1;

    cJSON* j = cJSON_Parse(data);
    free(data);
    if (!j) return -2;

    cJSON* tok      = cJSON_GetObjectItem(j, "mc_token");
    cJSON* saved_at = cJSON_GetObjectItem(j, "saved_at");
    cJSON* refresh  = cJSON_GetObjectItem(j, "ms_refresh");

    if (!tok || !tok->valuestring || !refresh || !refresh->valuestring) {
        cJSON_Delete(j);
        return -3;
    }

    if (saved_at) {
        double age = (double)time(NULL) - saved_at->valuedouble;
        if (age > MC_TOKEN_MAX_AGE_SECONDS) {
            log_msg("info", "ms_auth_validate: token expire (age=%.0fs)\n", age);
            cJSON_Delete(j);
            return -4;
        }
    }

    cJSON_Delete(j);
    return 0;
}

int ms_auth_login(const char* save_path) {
    char url[1024];
    snprintf(url, sizeof(url),
        "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize"
        "?client_id=%s"
        "&response_type=code"
        "&redirect_uri=" REDIRECT_URI
        "&scope=XboxLive.signin%%20offline_access"
        "&prompt=select_account",
        AZURE_CLIENT_ID);

    open_browser(url);

    char* code = wait_for_oauth_code();
    if (!code) { log_msg("error", "OAuth: no code received\n"); return -1; }
    log_msg("info", "OAuth: code received\n");

    char *ms_access=NULL, *ms_refresh=NULL;
    if (exchange_code_for_token(code, &ms_access, &ms_refresh) != 0) {
        free(code); log_msg("error", "OAuth: token exchange failed\n"); return -2;
    }
    free(code);

    char *xbl=NULL, *uhs=NULL;
    if (xbox_auth(ms_access, &xbl, &uhs) != 0) {
        log_msg("error", "OAuth: xbox auth failed\n");
        free(ms_access); free(ms_refresh);
        return -3;
    }

    char *xsts=NULL;
    if (xsts_auth(xbl, &xsts) != 0) {
        log_msg("error", "OAuth: xsts failed\n");
        free(ms_access); free(ms_refresh); free(xbl); free(uhs);
        return -4;
    }

    char *mc=NULL;
    if (mc_auth(xsts, uhs, &mc) != 0) {
        log_msg("error", "OAuth: mc auth failed\n");
        free(ms_access); free(ms_refresh); free(xbl); free(uhs); free(xsts);
        return -5;
    }

    char *uuid=NULL, *name=NULL;
    {
        int status = 0;
        char* prof_json = mc_profile(mc, &status);
        if (!prof_json || status != 200) {
            log_msg("error", "OAuth: profile fetch failed (status=%d)\n", status);
            free(prof_json);
            free(ms_access); free(ms_refresh);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -6;
        }
        cJSON* pj = cJSON_Parse(prof_json);
        free(prof_json);
        if (!pj) {
            log_msg("error", "OAuth: profile parse failed\n");
            free(ms_access); free(ms_refresh);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -7;
        }
        cJSON* jid   = cJSON_GetObjectItem(pj, "id");
        cJSON* jname = cJSON_GetObjectItem(pj, "name");
        if (!jid || !jname) {
            cJSON_Delete(pj);
            log_msg("error", "OAuth: profile missing fields\n");
            free(ms_access); free(ms_refresh);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -8;
        }
        uuid = strdup(jid->valuestring);
        name = strdup(jname->valuestring);
        cJSON_Delete(pj);
    }

    cJSON* j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "uuid", uuid);
    cJSON_AddStringToObject(j, "name", name);
    cJSON_AddStringToObject(j, "mc_token", mc);
    cJSON_AddStringToObject(j, "ms_refresh", ms_refresh);
    cJSON_AddNumberToObject(j, "saved_at", (double)time(NULL));
    char* out = cJSON_Print(j);
    write_file(save_path, out);
    free(out);
    cJSON_Delete(j);

    log_msg("info", "Login OK: %s\n", name);

    free(ms_access); free(ms_refresh);
    free(xbl); free(uhs); free(xsts); free(mc);
    free(uuid); free(name);
    return 0;
}

static int refresh_ms_token(const char* refresh_token,
                             char** new_ms_access,
                             char** new_ms_refresh)
{
    char body[2048];
    snprintf(body, sizeof(body),
        "client_id=%s"
        "&refresh_token=%s"
        "&grant_type=refresh_token"
        "&scope=XboxLive.signin%%20offline_access",
        AZURE_CLIENT_ID, refresh_token);

    int status = 0;
    char* resp = http_post_url(
        "login.microsoftonline.com",
        "/consumers/oauth2/v2.0/token",
        "application/x-www-form-urlencoded",
        body, strlen(body),
        &status
    );

    if (!resp) return -1;
    if (status != 200) {
        log_msg("error", "refresh_ms_token: HTTP %d\n", status);
        free(resp);
        return -2;
    }

    cJSON* j = cJSON_Parse(resp);
    free(resp);
    if (!j) return -3;

    cJSON* at = cJSON_GetObjectItem(j, "access_token");
    cJSON* rt = cJSON_GetObjectItem(j, "refresh_token");

    if (!at || !rt) {
        cJSON_Delete(j);
        return -4;
    }

    *new_ms_access  = strdup(at->valuestring);
    *new_ms_refresh = strdup(rt->valuestring);
    cJSON_Delete(j);
    return 0;
}

int ms_auth_refresh(const char* path) {
    char* data = read_file(path);
    if (!data) {
        log_msg("warn", "ms_auth_refresh: pas de compte sauvegardé\n");
        return -1;
    }

    cJSON* j = cJSON_Parse(data);
    free(data);
    if (!j) return -2;

    cJSON* jrt = cJSON_GetObjectItem(j, "ms_refresh");
    if (!jrt || !jrt->valuestring) {
        log_msg("error", "ms_auth_refresh: pas de ms_refresh\n");
        cJSON_Delete(j);
        return -3;
    }
    char* old_refresh = strdup(jrt->valuestring);
    cJSON_Delete(j);

    log_msg("info", "ms_auth_refresh: utilisation du refresh_token...\n");

    char *ms_access = NULL, *ms_refresh_new = NULL;
    if (refresh_ms_token(old_refresh, &ms_access, &ms_refresh_new) != 0) {
        log_msg("error", "ms_auth_refresh: refresh MS echoue\n");
        free(old_refresh);
        return -4;
    }
    free(old_refresh);

    char *xbl = NULL, *uhs = NULL;
    if (xbox_auth(ms_access, &xbl, &uhs) != 0) {
        log_msg("error", "ms_auth_refresh: xbox_auth echoue\n");
        free(ms_access); free(ms_refresh_new);
        return -5;
    }

    char* xsts = NULL;
    if (xsts_auth(xbl, &xsts) != 0) {
        log_msg("error", "ms_auth_refresh: xsts_auth echoue\n");
        free(ms_access); free(ms_refresh_new);
        free(xbl); free(uhs);
        return -6;
    }

    char* mc = NULL;
    if (mc_auth(xsts, uhs, &mc) != 0) {
        log_msg("error", "ms_auth_refresh: mc_auth echoue\n");
        free(ms_access); free(ms_refresh_new);
        free(xbl); free(uhs); free(xsts);
        return -7;
    }

    char *uuid = NULL, *name = NULL;
    {
        int status = 0;
        char* prof_json = mc_profile(mc, &status);
        if (!prof_json || status != 200) {
            log_msg("error", "ms_auth_refresh: profil MC inaccessible (status=%d)\n", status);
            free(prof_json);
            free(ms_access); free(ms_refresh_new);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -8;
        }
        cJSON* pj = cJSON_Parse(prof_json);
        free(prof_json);
        if (!pj) {
            free(ms_access); free(ms_refresh_new);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -9;
        }

        cJSON* jid   = cJSON_GetObjectItem(pj, "id");
        cJSON* jname = cJSON_GetObjectItem(pj, "name");
        if (!jid || !jname) {
            cJSON_Delete(pj);
            free(ms_access); free(ms_refresh_new);
            free(xbl); free(uhs); free(xsts); free(mc);
            return -10;
        }
        uuid = strdup(jid->valuestring);
        name = strdup(jname->valuestring);
        cJSON_Delete(pj);
    }

    cJSON* out_j = cJSON_CreateObject();
    cJSON_AddStringToObject(out_j, "uuid",       uuid);
    cJSON_AddStringToObject(out_j, "name",       name);
    cJSON_AddStringToObject(out_j, "mc_token",   mc);
    cJSON_AddStringToObject(out_j, "ms_refresh", ms_refresh_new);
    cJSON_AddNumberToObject(out_j, "saved_at",   (double)time(NULL));

    char* out_str = cJSON_Print(out_j);
    write_file(path, out_str);
    free(out_str);
    cJSON_Delete(out_j);

    log_msg("info", "ms_auth_refresh: OK, compte rafraichi pour %s\n", name);

    free(ms_access); free(ms_refresh_new);
    free(xbl); free(uhs); free(xsts); free(mc);
    free(uuid); free(name);
    return 0;
}
