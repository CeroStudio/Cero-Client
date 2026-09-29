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

static inline void java_get_required_component(const char* client_dir, const char* version_id,
                                               char* out, size_t size) {
    char path[MAX_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/versions/%s/%s.json",
            client_dir, version_id, version_id);

    snprintf(out, size, "jre-legacy");

    VmJVal* root = vm_load_json(path);
    if (!root) return;

    VmJVal* jv = vm_get(root, "javaVersion");
    if (jv) {
        const char* component = vm_gets(jv, "component");
        if (component && component[0]) {
            snprintf(out, size, "%s", component);
        }
    }
    vm_free(root);
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

#define MOJANG_JAVA_RUNTIME_MANIFEST_URL \
    "https://piston-meta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json"

static inline const char* java_mojang_os_key(void) {
#if defined(_WIN32)
    return "windows-x64";
#elif defined(__APPLE__)
  #if defined(__aarch64__) || defined(__arm64__)
    return "mac-os-arm64";
  #else
    return "mac-os";
  #endif
#else
    return "linux";
#endif
}

static inline int java_resolve_mojang_manifest_url(const char* component,
                                                   char* url_out, size_t size) {
    char tmp_path[] = "java_runtime_manifest.json";

    download_file(MOJANG_JAVA_RUNTIME_MANIFEST_URL, tmp_path);
    if (!java_file_exists(tmp_path)) return 0;

    VmJVal* root = vm_load_json(tmp_path);
    remove(tmp_path);
    if (!root) return 0;

    int ok = 0;
    VmJVal* os_section = vm_get(root, java_mojang_os_key());
    if (os_section) {
        VmJVal* comp_arr = vm_get(os_section, component);
        if (comp_arr && comp_arr->t == VM_JARR && comp_arr->a.count > 0) {
            VmJVal* first = comp_arr->a.items[0];
            VmJVal* manifest = vm_get(first, "manifest");
            const char* murl = manifest ? vm_gets(manifest, "url") : NULL;
            if (murl) {
                snprintf(url_out, size, "%s", murl);
                ok = 1;
            }
        }
    }
    vm_free(root);
    return ok;
}

static inline void java_mkdirs_rel(const char* out_dir, const char* rel_dir) {
    char full[MAX_PATH_SIZE];
    snprintf(full, sizeof(full), "%s/%s", out_dir, rel_dir);
    ensure_directory_exists(full);
}

static inline void java_chmod_exec(const char* path) {
#if !defined(_WIN32)
    char cmd[MAX_PATH_SIZE + 16];
    snprintf(cmd, sizeof(cmd), "chmod +x \"%s\"", path);
    system(cmd);
#else
    (void)path;
#endif
}

static inline int java_download_mojang_runtime(const char* manifest_url,
                                               const char* install_dir) {
    char tmp_path[] = "java_files_manifest.json";
    download_file(manifest_url, tmp_path);
    if (!java_file_exists(tmp_path)) return 0;

    VmJVal* root = vm_load_json(tmp_path);
    remove(tmp_path);
    if (!root) return 0;

    VmJVal* files = vm_get(root, "files");
    if (!files || files->t != VM_JOBJ) {
        vm_free(root);
        return 0;
    }

    size_t downloaded = 0;
    for (size_t i = 0; i < files->o.count; i++) {
        const char* rel_path = files->o.pairs[i].k;
        VmJVal* entry = files->o.pairs[i].v;
        const char* type = vm_gets(entry, "type");
        if (!type) continue;

        if (strcmp(type, "directory") == 0) {
            java_mkdirs_rel(install_dir, rel_path);
            continue;
        }
        if (strcmp(type, "file") != 0) {
            continue;
        }

        VmJVal* downloads = vm_get(entry, "downloads");
        VmJVal* raw = downloads ? vm_get(downloads, "raw") : NULL;
        const char* file_url = raw ? vm_gets(raw, "url") : NULL;
        if (!file_url) continue;

        char out_path[MAX_PATH_SIZE];
        snprintf(out_path, sizeof(out_path), "%s/%s", install_dir, rel_path);
        ensure_parent_dirs(out_path);

        download_file(file_url, out_path);
        if (!java_file_exists(out_path)) {
            log_msg("warn", "Fichier JRE manquant apres telechargement: %s\n", rel_path);
            continue;
        }

        double executable = 0;
        VmJVal* exec_val = vm_get(entry, "executable");
        if (exec_val && exec_val->t == VM_JBOOL) executable = exec_val->b;
        if (executable) java_chmod_exec(out_path);

        downloaded++;
    }

    vm_free(root);
    return downloaded > 0;
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

static inline int java_ensure(const char* client_dir, const char* version_id) {
    int major = java_get_required_version(client_dir, version_id);
    log_msg("info", "Minecraft %s requires Java %d\n", version_id, major);

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

    char component[64];
    java_get_required_component(client_dir, version_id, component, sizeof(component));

    log_msg("info", "Downloading Java %d (%s, composant Mojang '%s')...\n",
            major, JAVA_OS, component);

    char files_manifest_url[512];
    if (!java_resolve_mojang_manifest_url(component, files_manifest_url, sizeof(files_manifest_url))) {
        log_msg("error",
            "Aucun runtime Mojang '%s' disponible pour cet OS/architecture (Java %d)\n",
            component, major);
        return 0;
    }

    char install_dir[MAX_PATH_SIZE];
    java_get_install_dir(client_dir, major, install_dir, sizeof(install_dir));
    ensure_directory_exists(install_dir);

    log_msg("info", "Telechargement du runtime Java %d (fichier par fichier)...\n", major);
    if (!java_download_mojang_runtime(files_manifest_url, install_dir)) {
        log_msg("error", "Echec du telechargement du runtime Java %d\n", major);
        return 0;
    }

    if (!java_is_installed(client_dir, major)) {
        log_msg("error", "Java %d install failed (executable not found)\n", major);
        return 0;
    }

    log_msg("succes", "Java %d installed at %s\n", major, install_dir);
    return 1;
#endif
}

#endif