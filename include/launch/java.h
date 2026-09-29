#ifndef JAVA_H
#define JAVA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../config/config.h"
#include "../core/logger.h"
#include "../net/download.h"
#include "../vendor/miniz.h"
#include "../utils/file_utils.h"
#include "../utils/version_manifest.h"

#if defined(_WIN32)
  #define JAVA_OS      "windows"
  #define JAVA_ARCH    "x64"
  #define JAVA_EXT     ".zip"
  #define JAVA_BIN     "bin\\java.exe"
#elif defined(__APPLE__)
  #define JAVA_OS      "mac"
  #if defined(__aarch64__) || defined(__arm64__)
    #define JAVA_ARCH  "aarch64"
  #else
    #define JAVA_ARCH  "x64"
  #endif
  #define JAVA_EXT     ".tar.gz"
  #define JAVA_BIN     "Contents/Home/bin/java"
#elif defined(__FreeBSD__)
  #define JAVA_FREEBSD 1
  #define JAVA_BIN     "bin/java"
#else
  #define JAVA_OS      "linux"
  #define JAVA_ARCH    "x64"
  #define JAVA_EXT     ".tar.gz"
  #define JAVA_BIN     "bin/java"
#endif

static inline int java_file_exists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

static inline int java_get_required_version(const char* client_dir, const char* version_id) {
    char path[MAX_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/versions/%s/%s.json",
            client_dir, version_id, version_id);

    VmJVal* root = vm_load_json(path);
    if (!root) return 8;

    VmJVal* jv = vm_get(root, "javaVersion");
    int major = 8;
    if (jv) {
        double v = vm_getn(jv, "majorVersion");
        if (v > 0) major = (int)v;
    }
    vm_free(root);
    return major;
}

static inline void java_get_install_dir(const char* client_dir, int major, char* out, size_t size) {
    snprintf(out, size, "%s/runtime/java%d", client_dir, major);
}

static inline void java_get_executable(const char* client_dir, int major, char* out, size_t size) {
    char dir[MAX_PATH_SIZE];
    java_get_install_dir(client_dir, major, dir, sizeof(dir));
    snprintf(out, size, "%s/%s", dir, JAVA_BIN);
}

#ifdef JAVA_FREEBSD
static inline int java_find_system(int major, char* out, size_t size) {
    char cand[MAX_PATH_SIZE];

    snprintf(cand, sizeof(cand), "/usr/local/openjdk%d/bin/java", major);
    if (java_file_exists(cand)) { snprintf(out, size, "%s", cand); return 1; }

    snprintf(cand, sizeof(cand), "/usr/local/openjdk%d-jre/bin/java", major);
    if (java_file_exists(cand)) { snprintf(out, size, "%s", cand); return 1; }

    static const char* generic[] = {
        "/usr/local/bin/java",
        "/usr/bin/java",
    };
    for (size_t i = 0; i < sizeof(generic)/sizeof(generic[0]); i++) {
        if (java_file_exists(generic[i])) {
            snprintf(out, size, "%s", generic[i]);
            return 1;
        }
    }
    return 0;
}
#endif

static inline int java_resolve_executable(const char* client_dir, int major,
                                          char* out, size_t size) {
#ifdef JAVA_FREEBSD
    (void)client_dir;
    return java_find_system(major, out, size);
#else
    java_get_executable(client_dir, major, out, size);
    return java_file_exists(out);
#endif
}

static inline int java_is_installed(const char* client_dir, int major) {
#ifdef JAVA_FREEBSD
    char exe[MAX_PATH_SIZE];
    return java_find_system(major, exe, sizeof(exe));
#else
    char exe[MAX_PATH_SIZE];
    java_get_executable(client_dir, major, exe, sizeof(exe));
    return java_file_exists(exe);
#endif
}

#ifndef JAVA_FREEBSD
static inline int java_query_download_link(int major, const char* arch,
                                           char* url_out, size_t size) {
    char api_url[512];
    snprintf(api_url, sizeof(api_url),
        "https://api.adoptium.net/v3/assets/latest/%d/hotspot?os=%s&architecture=%s&image_type=jre",
        major, JAVA_OS, arch);

    char tmp_path[MAX_PATH_SIZE];
    snprintf(tmp_path, sizeof(tmp_path), "java_api_%d.json", major);

    download_file(api_url, tmp_path);

    int got_link = 0;
    if (java_file_exists(tmp_path)) {
        VmJVal* root = vm_load_json(tmp_path);
        if (root && root->t == VM_JARR && root->a.count > 0) {
            VmJVal* first = root->a.items[0];
            VmJVal* binary = vm_get(first, "binary");
            VmJVal* package = binary ? vm_get(binary, "package") : NULL;
            const char* link = package ? vm_gets(package, "link") : NULL;
            if (link) {
                snprintf(url_out, size, "%s", link);
                got_link = 1;
            }
        }
        if (root) vm_free(root);
        remove(tmp_path);
    }
    return got_link;
}

static inline int java_fetch_download_url(int major, char* url_out, size_t size) {
    if (java_query_download_link(major, JAVA_ARCH, url_out, size)) return 1;

#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    log_msg("warn", "No aarch64 JRE for Java %d, falling back to x64 (needs Rosetta 2)\n", major);
    if (java_query_download_link(major, "x64", url_out, size)) return 1;
#endif

    snprintf(url_out, size,
        "https://api.adoptium.net/v3/binary/latest/%d/ga/%s/%s/jre/hotspot/normal/eclipse",
        major, JAVA_OS, JAVA_ARCH);
    return 1;
}

static inline int java_extract_zip(const char* zip_path, const char* out_dir) {
#if defined(_WIN32)
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return 0;

    int n = (int)mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        if (st.m_is_directory) continue;

        const char* rel = strchr(st.m_filename, '/');
        if (!rel) continue;
        rel++;
        if (!*rel) continue;

        char out_path[MAX_PATH_SIZE];
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, rel);
        ensure_parent_dirs(out_path);
        mz_zip_reader_extract_to_file(&zip, i, out_path, 0);
    }
    mz_zip_reader_end(&zip);
    return 1;
#else
    char cmd[MAX_PATH_SIZE * 2];
    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" --strip-components=1", zip_path, out_dir);
    return system(cmd) == 0;
#endif
}
#endif

static inline int java_round_up_to_lts(int major) {
    static const int lts[] = {8, 11, 17, 21, 25};
    for (size_t i = 0; i < sizeof(lts)/sizeof(lts[0]); i++) {
        if (lts[i] >= major) return lts[i];
    }
    return major;
}

static inline int java_ensure(const char* client_dir, const char* version_id) {
    int required_major = java_get_required_version(client_dir, version_id);
    int major = java_round_up_to_lts(required_major);
    if (major != required_major) {
        log_msg("info",
            "Minecraft %s requires Java %d (non-LTS, indisponible sur Adoptium) "
            "-> utilisation de Java %d (LTS)\n",
            version_id, required_major, major);
    } else {
        log_msg("info", "Minecraft %s requires Java %d\n", version_id, major);
    }

#ifdef JAVA_FREEBSD
    char sys_java[MAX_PATH_SIZE];
    if (java_find_system(major, sys_java, sizeof(sys_java))) {
        log_msg("succes", "Java %d systeme trouve : %s\n", major, sys_java);
        return 1;
    }
    log_msg("error",
        "Java %d introuvable. Adoptium ne fournit pas de build FreeBSD.\n"
        "Installez-le : sudo pkg install openjdk%d\n", major, major);
    return 0;
#else
    if (java_is_installed(client_dir, major)) {
        log_msg("info", "Java %d already installed\n", major);
        return 1;
    }

    log_msg("info", "Downloading Java %d (%s)...\n", major, JAVA_OS);

    char url[1024];
    if (!java_fetch_download_url(major, url, sizeof(url))) {
        log_msg("error", "Cannot get download URL for Java %d\n", major);
        return 0;
    }

    char archive_path[MAX_PATH_SIZE];
    snprintf(archive_path, sizeof(archive_path),
             "%s/runtime/java%d_download%s", client_dir, major, JAVA_EXT);
    ensure_parent_dirs(archive_path);

    download_file(url, archive_path);
    if (!java_file_exists(archive_path)) {
        log_msg("error", "Failed to download Java %d\n", major);
        return 0;
    }

    char install_dir[MAX_PATH_SIZE];
    java_get_install_dir(client_dir, major, install_dir, sizeof(install_dir));
    ensure_directory_exists(install_dir);

    log_msg("info", "Extracting Java %d...\n", major);
    if (!java_extract_zip(archive_path, install_dir)) {
        log_msg("error", "Failed to extract Java %d\n", major);
        return 0;
    }

    remove(archive_path);

    if (!java_is_installed(client_dir, major)) {
        log_msg("error", "Java %d install failed (executable not found)\n", major);
        return 0;
    }

    log_msg("succes", "Java %d installed at %s\n", major, install_dir);
    return 1;
#endif
}

#endif