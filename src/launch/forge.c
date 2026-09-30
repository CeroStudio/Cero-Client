#include "../../include/launch/forge.h"
#include "../../include/launch/classpath.h"
#include "../../include/launch/jvm_args.h"
#include "../../include/launch/mc_json.h"
#include "../../include/net/download.h"
#include "../../include/utils/file_utils.h"
#include "../../include/platform/paths.h"
#include "../../include/crypto/sha1.h"
#include "../../include/utils/process.h"
#include "../../include/vendor/cJSON.h"
#include "../../include/vendor/miniz.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORGE_MAVEN "https://maven.minecraftforge.net/"
#define FORGE_PROMOS "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"

#define NEOFORGE_MAVEN "https://maven.neoforged.net/releases/"
#define NEOFORGE_VERSIONS_API "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"

int parse_forge_spec(const char* spec, char* mc, size_t mcsz,
                     char* loader, size_t loadersz) {
    loader[0] = '\0';
    if (strncmp(spec, "forge:", 6) != 0) return 0;
    const char* p = spec + 6;
    const char* colon = strchr(p, ':');
    if (colon) {
        size_t n = (size_t)(colon - p);
        if (n >= mcsz) n = mcsz - 1;
        memcpy(mc, p, n); mc[n] = '\0';
        snprintf(loader, loadersz, "%s", colon + 1);
    } else {
        snprintf(mc, mcsz, "%s", p);
    }
    return 1;
}

/* ---- maven GAV -> relative jar path: shared with classpath.c ---- */

static int zip_extract_entry(const char* zip_path, const char* entry_name, const char* dest_path) {
    if (entry_name[0] == '/') entry_name++;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return 0;
    ensure_parent_dirs(dest_path);
    int ok = mz_zip_reader_extract_file_to_file(&zip, entry_name, dest_path, 0);
    mz_zip_reader_end(&zip);
    if (!ok) log_msg("error", "Forge: cannot extract '%s' from %s\n", entry_name, zip_path);
    return ok;
}

static char* zip_extract_to_mem(const char* zip_path, const char* entry_name, size_t* out_sz) {
    if (entry_name[0] == '/') entry_name++;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return NULL;
    size_t sz = 0;
    void* buf = mz_zip_reader_extract_file_to_heap(&zip, entry_name, &sz, 0);
    mz_zip_reader_end(&zip);
    if (!buf) return NULL;
    char* txt = malloc(sz + 1);
    if (!txt) { free(buf); return NULL; }
    memcpy(txt, buf, sz);
    txt[sz] = '\0';
    free(buf);
    if (out_sz) *out_sz = sz;
    return txt;
}

static int jar_main_class(const char* jar_path, char* out, size_t outsz) {
    size_t sz = 0;
    char* txt = zip_extract_to_mem(jar_path, "META-INF/MANIFEST.MF", &sz);
    if (!txt) return 0;
    int ok = 0;
    char* p = strstr(txt, "Main-Class:");
    if (p) {
        p += strlen("Main-Class:");
        while (*p == ' ') p++;
        size_t i = 0;
        while (*p && *p != '\r' && *p != '\n' && i + 1 < outsz) out[i++] = *p++;
        out[i] = '\0';
        ok = 1;
    }
    free(txt);
    return ok;
}

/* Downloads every library referenced by a (cJSON) "libraries" array. Libs
 * with no download URL are assumed to live inside the installer jar's
 * embedded "maven/" mini-repository (this is how Forge ships its own
 * client/universal jar to itself). */
/* Guards against the "half-downloaded / HTML error page saved as .jar"
 * case: without this, a corrupted file from an earlier interrupted run
 * looks like "it exists" forever and every later launch fails at the
 * exact same processor step (jar_main_class() can't parse a manifest
 * out of it) with no way to recover short of the user manually deleting
 * the file. */
static int is_valid_jar_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char magic[4] = {0};
    size_t n = fread(magic, 1, 4, f);
    fclose(f);
    /* ZIP/JAR local file header signature: 'P' 'K' 0x03 0x04 */
    return n == 4 && magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04;
}

static void download_cjson_libraries(const char* client_dir, const char* installer_jar, cJSON* libs) {
    if (!cJSON_IsArray(libs)) return;
    cJSON* lib;
    cJSON_ArrayForEach(lib, libs) {
        cJSON* name = cJSON_GetObjectItem(lib, "name");
        if (!cJSON_IsString(name)) continue;

        char relpath[512];
        if (!gav_to_relpath(name->valuestring, relpath, sizeof(relpath))) continue;

        char path[MAX_PATH_SIZE];
        snprintf(path, sizeof(path), "%s/libraries/%s", client_dir, relpath);

        if (file_exists(path)) {
            if (is_valid_jar_file(path)) continue;
            log_msg("info", "Cached lib %s looks corrupted, re-downloading\n", relpath);
            remove(path);
        }
        ensure_parent_dirs(path);

        /* Two different shapes here:
         * - "downloads.artifact.url" (the standard Mojang-style schema) is
         *   already the complete, exact URL to the jar — used as-is.
         * - a flat top-level "url" (Fabric/legacy-Forge style) is only a
         *   maven *root*, and relpath must be appended to it. */
        const char* full_artifact_url = NULL;
        const char* maven_base = NULL;

        cJSON* downloads = cJSON_GetObjectItem(lib, "downloads");
        cJSON* artifact = downloads ? cJSON_GetObjectItem(downloads, "artifact") : NULL;
        cJSON* u = artifact ? cJSON_GetObjectItem(artifact, "url") : NULL;
        if (cJSON_IsString(u) && u->valuestring[0]) full_artifact_url = u->valuestring;

        if (!full_artifact_url) {
            cJSON* u2 = cJSON_GetObjectItem(lib, "url");
            if (cJSON_IsString(u2) && u2->valuestring[0]) maven_base = u2->valuestring;
        }

        int ok = 0;
        if (full_artifact_url) {
            log_msg("info", "Downloading Forge lib: %s\n", full_artifact_url);
            ok = download_file(full_artifact_url, path);
        } else if (maven_base) {
            char full_url[1024];
            const char* sep = (maven_base[strlen(maven_base) - 1] == '/') ? "" : "/";
            snprintf(full_url, sizeof(full_url), "%s%s%s", maven_base, sep, relpath);
            log_msg("info", "Downloading Forge lib: %s\n", full_url);
            ok = download_file(full_url, path);
        } else {
            char entry[600];
            snprintf(entry, sizeof(entry), "maven/%s", relpath);
            log_msg("info", "Extracting Forge lib from installer: %s\n", entry);
            ok = zip_extract_entry(installer_jar, entry, path);
        }

        if (!ok || !is_valid_jar_file(path)) {
            log_msg("error", "Failed to obtain a valid jar for %s (%s)\n", name->valuestring, path);
            remove(path);
        }
    }
}

static void resolve_data_value(const char* client_dir, const char* installer_jar,
                               const char* raw, char* out, size_t outsz) {
    size_t len = strlen(raw);
    if (len >= 2 && raw[0] == '[' && raw[len - 1] == ']') {
        char gav[256];
        snprintf(gav, sizeof(gav), "%.*s", (int)(len - 2), raw + 1);
        char relpath[512];
        gav_to_relpath(gav, relpath, sizeof(relpath));
        snprintf(out, outsz, "%s/libraries/%s", client_dir, relpath);
    } else if (len >= 2 && raw[0] == '\'' && raw[len - 1] == '\'') {
        snprintf(out, outsz, "%.*s", (int)(len - 2), raw + 1);
    } else if (raw[0] == '/') {
        char dest[MAX_PATH_SIZE];
        snprintf(dest, sizeof(dest), "%s/forge/extracted%s", client_dir, raw);
        if (!file_exists(dest)) zip_extract_entry(installer_jar, raw + 1, dest);
        snprintf(out, outsz, "%s", dest);
    } else {
        snprintf(out, outsz, "%s", raw);
    }
}

static int lookup_data_token(cJSON* data, const char* client_dir, const char* installer_jar,
                             const char* token, char* out, size_t outsz) {
    cJSON* entry = cJSON_GetObjectItem(data, token);
    if (!entry) return 0;
    cJSON* val = cJSON_GetObjectItem(entry, "client");
    if (!cJSON_IsString(val)) return 0;
    resolve_data_value(client_dir, installer_jar, val->valuestring, out, outsz);
    return 1;
}

static void substitute_arg(cJSON* data, const char* client_dir, const char* installer_jar,
                           const char* vanilla_jar, const char* mc_version,
                           const char* arg, char* out, size_t outsz) {
    size_t len = strlen(arg);
    if (len >= 2 && arg[0] == '{' && arg[len - 1] == '}') {
        char token[128];
        snprintf(token, sizeof(token), "%.*s", (int)(len - 2), arg + 1);

        if (strcmp(token, "SIDE") == 0)              { snprintf(out, outsz, "client"); return; }
        if (strcmp(token, "MINECRAFT_JAR") == 0)      { snprintf(out, outsz, "%s", vanilla_jar); return; }
        if (strcmp(token, "MINECRAFT_VERSION") == 0)  { snprintf(out, outsz, "%s", mc_version); return; }
        if (strcmp(token, "ROOT") == 0)               { snprintf(out, outsz, "%s", client_dir); return; }
        if (strcmp(token, "INSTALLER") == 0)          { snprintf(out, outsz, "%s", installer_jar); return; }
        if (strcmp(token, "LIBRARY_DIR") == 0)        { snprintf(out, outsz, "%s/libraries", client_dir); return; }

        char resolved[MAX_PATH_SIZE];
        if (lookup_data_token(data, client_dir, installer_jar, token, resolved, sizeof(resolved))) {
            snprintf(out, outsz, "%s", resolved);
            return;
        }
        snprintf(out, outsz, "%s", arg);
        return;
    }
    if (len >= 2 && arg[0] == '[' && arg[len - 1] == ']') {
        char gav[256];
        snprintf(gav, sizeof(gav), "%.*s", (int)(len - 2), arg + 1);
        char relpath[512];
        gav_to_relpath(gav, relpath, sizeof(relpath));
        snprintf(out, outsz, "%s/libraries/%s", client_dir, relpath);
        return;
    }
    snprintf(out, outsz, "%s", arg);
}

static int processor_outputs_up_to_date(cJSON* proc, cJSON* data, const char* client_dir,
                                        const char* installer_jar, const char* vanilla_jar,
                                        const char* mc_version) {
    cJSON* outputs = cJSON_GetObjectItem(proc, "outputs");
    if (!cJSON_IsObject(outputs)) return 0;

    int had_any = 0;
    cJSON* kv;
    cJSON_ArrayForEach(kv, outputs) {
        had_any = 1;
        char file_path[MAX_PATH_SIZE];
        substitute_arg(data, client_dir, installer_jar, vanilla_jar, mc_version,
                      kv->string, file_path, sizeof(file_path));

        if (!file_exists(file_path)) return 0;

        if (cJSON_IsString(kv)) {
            char expected_sha1[128];
            substitute_arg(data, client_dir, installer_jar, vanilla_jar, mc_version,
                          kv->valuestring, expected_sha1, sizeof(expected_sha1));
            if (expected_sha1[0] && !sha1_file_matches(file_path, expected_sha1)) return 0;
        }
    }
    return had_any;
}

static int run_processor(cJSON* proc, cJSON* data, const char* client_dir,
                         const char* installer_jar, const char* java_exe,
                         const char* vanilla_jar, const char* mc_version) {
    cJSON* sides = cJSON_GetObjectItem(proc, "sides");
    if (cJSON_IsArray(sides)) {
        int has_client = 0;
        cJSON* s;
        cJSON_ArrayForEach(s, sides) {
            if (cJSON_IsString(s) && strcmp(s->valuestring, "client") == 0) has_client = 1;
        }
        if (!has_client) return 1;
    }

    if (processor_outputs_up_to_date(proc, data, client_dir, installer_jar, vanilla_jar, mc_version)) {
        log_msg("info", "Forge processor already applied, skipping\n");
        return 1;
    }

    cJSON* jar_field = cJSON_GetObjectItem(proc, "jar");
    if (!cJSON_IsString(jar_field)) return 0;

    char relpath[512];
    gav_to_relpath(jar_field->valuestring, relpath, sizeof(relpath));
    char proc_jar[MAX_PATH_SIZE];
    snprintf(proc_jar, sizeof(proc_jar), "%s/libraries/%s", client_dir, relpath);

    char main_class[256];
    if (!jar_main_class(proc_jar, main_class, sizeof(main_class))) {
        log_msg("error", "Forge processor: cannot read Main-Class from %s\n", proc_jar);
        remove(proc_jar); /* force a clean re-download on the next launch attempt */
        return 0;
    }

    static char classpath[65536];
    size_t cp_len = (size_t)snprintf(classpath, sizeof(classpath), "%s", proc_jar);

    cJSON* cp = cJSON_GetObjectItem(proc, "classpath");
    cJSON* cp_lib;
    cJSON_ArrayForEach(cp_lib, cp) {
        if (!cJSON_IsString(cp_lib) || cp_len >= sizeof(classpath)) continue;
        char lib_relpath[512];
        gav_to_relpath(cp_lib->valuestring, lib_relpath, sizeof(lib_relpath));
        char libpath[MAX_PATH_SIZE];
        snprintf(libpath, sizeof(libpath), "%s/libraries/%s", client_dir, lib_relpath);
        cp_len += (size_t)snprintf(classpath + cp_len, sizeof(classpath) - cp_len,
                                   "%s%s", CP_SEP, libpath);
    }

    static char resolved_args[64][MAX_PATH_SIZE];
    const char* argv[80];
    int argc = 0;
    argv[argc++] = java_exe;
    argv[argc++] = "-cp";
    argv[argc++] = classpath;
    argv[argc++] = main_class;

    int ai = 0;
    cJSON* args = cJSON_GetObjectItem(proc, "args");
    cJSON* a;
    cJSON_ArrayForEach(a, args) {
        if (!cJSON_IsString(a) || ai >= 64 || argc >= 78) continue;
        substitute_arg(data, client_dir, installer_jar, vanilla_jar, mc_version,
                      a->valuestring, resolved_args[ai], sizeof(resolved_args[ai]));
        argv[argc++] = resolved_args[ai++];
    }
    argv[argc] = NULL;

    log_msg("info", "Running Forge processor: %s\n", main_class);
    int rc = process_run(java_exe, argv);
    if (rc != 0) {
        log_msg("error", "Forge processor %s exited with code %d\n", main_class, rc);
        return 0;
    }
    return 1;
}

int fetch_forge_profile(const LaunchCtx* ctx, const char* client_dir,
                        const char* mc_version, const char* forge_version_in,
                        const char* java_exe,
                        const char* vanilla_jar, const char* vanilla_json,
                        char* out_id, size_t out_id_sz,
                        char* out_json_path, size_t out_json_sz) {
    (void)vanilla_json;

    char forge_version[64];
    snprintf(forge_version, sizeof(forge_version), "%s", forge_version_in ? forge_version_in : "");

    if (!forge_version[0]) {
        launch_report(ctx, "Recherche de la version Forge recommandée...", 90);
        char promos_path[MAX_PATH_SIZE];
        snprintf(promos_path, sizeof(promos_path), "%s/cache/forge_promotions.json", client_dir);
        ensure_parent_dirs(promos_path);
        if (!download_file(FORGE_PROMOS, promos_path)) {
            log_msg("error", "Cannot fetch Forge promotions list\n");
            return 0;
        }
        char* txt = read_file(promos_path);
        if (!txt) return 0;
        cJSON* root = cJSON_Parse(txt);
        free(txt);
        if (!root) return 0;

        cJSON* promos = cJSON_GetObjectItem(root, "promos");
        char key_recommended[96], key_latest[96];
        snprintf(key_recommended, sizeof(key_recommended), "%s-recommended", mc_version);
        snprintf(key_latest, sizeof(key_latest), "%s-latest", mc_version);

        cJSON* found = cJSON_GetObjectItem(promos, key_recommended);
        if (!cJSON_IsString(found)) found = cJSON_GetObjectItem(promos, key_latest);

        if (!cJSON_IsString(found)) {
            log_msg("error", "No Forge build found for Minecraft %s\n", mc_version);
            cJSON_Delete(root);
            return 0;
        }
        snprintf(forge_version, sizeof(forge_version), "%s", found->valuestring);
        cJSON_Delete(root);
        log_msg("info", "Using Forge version: %s\n", forge_version);
    }

    char full_version[160];
    snprintf(full_version, sizeof(full_version), "%s-%s", mc_version, forge_version);

    char installer_url[512];
    snprintf(installer_url, sizeof(installer_url),
            "%snet/minecraftforge/forge/%s/forge-%s-installer.jar",
            FORGE_MAVEN, full_version, full_version);

    char installer_path[MAX_PATH_SIZE];
    snprintf(installer_path, sizeof(installer_path),
            "%s/forge/installers/forge-%s-installer.jar", client_dir, full_version);

    if (!file_exists(installer_path)) {
        launch_report(ctx, "Téléchargement de l'installateur Forge...", 91);
        ensure_parent_dirs(installer_path);
        if (!download_file(installer_url, installer_path)) {
            log_msg("error", "Cannot download Forge installer from %s\n", installer_url);
            return 0;
        }
    }

    size_t profile_sz = 0;
    char* profile_txt = zip_extract_to_mem(installer_path, "install_profile.json", &profile_sz);
    if (!profile_txt) {
        log_msg("error",
               "install_profile.json not found in %s (Forge < 1.6 binpatch-based installers are not supported)\n",
               installer_path);
        return 0;
    }
    cJSON* profile = cJSON_Parse(profile_txt);
    free(profile_txt);
    if (!profile) return 0;

    cJSON* version_info = cJSON_GetObjectItem(profile, "versionInfo");
    cJSON* processors_probe = cJSON_GetObjectItem(profile, "processors");
    int is_legacy = version_info != NULL &&
                   !(cJSON_IsArray(processors_probe) && cJSON_GetArraySize(processors_probe) > 0);

    if (is_legacy) {
        /* Legacy Forge (1.6 - 1.12.2): the launch profile is embedded
         * directly, no install-time processors to run. */
        cJSON* idf = cJSON_GetObjectItem(version_info, "id");
        if (cJSON_IsString(idf)) {
            snprintf(out_id, out_id_sz, "%s", idf->valuestring);
        } else {
            snprintf(out_id, out_id_sz, "%s-forge-%s", mc_version, forge_version);
        }

        snprintf(out_json_path, out_json_sz, "%s/versions/%s/%s.json", client_dir, out_id, out_id);
        ensure_parent_dirs(out_json_path);
        char* version_txt = cJSON_Print(version_info);
        if (!version_txt) { cJSON_Delete(profile); return 0; }
        FILE* vf = fopen(out_json_path, "wb");
        if (vf) { fputs(version_txt, vf); fclose(vf); }
        free(version_txt);

        launch_report(ctx, "Téléchargement des librairies Forge...", 93);
        download_cjson_libraries(client_dir, installer_path, cJSON_GetObjectItem(version_info, "libraries"));

        /* Some very old installers store the Forge universal jar only at
         * the zip root (not mirrored under "maven/"); make sure it lands
         * at its expected maven path either way. */
        cJSON* install_section = cJSON_GetObjectItem(profile, "install");
        if (install_section) {
            cJSON* path_field = cJSON_GetObjectItem(install_section, "path");
            cJSON* file_field = cJSON_GetObjectItem(install_section, "filePath");
            if (cJSON_IsString(path_field) && cJSON_IsString(file_field)) {
                char relpath[512];
                if (gav_to_relpath(path_field->valuestring, relpath, sizeof(relpath))) {
                    char dest[MAX_PATH_SIZE];
                    snprintf(dest, sizeof(dest), "%s/libraries/%s", client_dir, relpath);
                    if (!file_exists(dest)) {
                        ensure_parent_dirs(dest);
                        zip_extract_entry(installer_path, file_field->valuestring, dest);
                    }
                }
            }
        }

        cJSON_Delete(profile);
        return 1;
    }

    /* Modern installers nest most of this under "install" + reference the
     * real launch json via "json" (a path inside the jar). */
    cJSON* install_section = cJSON_GetObjectItem(profile, "install");
    cJSON* json_field = cJSON_GetObjectItem(profile, "json");
    if (!json_field && install_section) json_field = cJSON_GetObjectItem(install_section, "json");

    const char* version_json_entry = cJSON_IsString(json_field) ? json_field->valuestring : "/version.json";

    cJSON* version_field = cJSON_GetObjectItem(profile, "version");
    if (cJSON_IsString(version_field)) {
        snprintf(out_id, out_id_sz, "%s", version_field->valuestring);
    } else {
        snprintf(out_id, out_id_sz, "%s-forge-%s", mc_version, forge_version);
    }

    snprintf(out_json_path, out_json_sz, "%s/versions/%s/%s.json", client_dir, out_id, out_id);
    if (!zip_extract_entry(installer_path, version_json_entry, out_json_path)) {
        cJSON_Delete(profile);
        return 0;
    }

    launch_report(ctx, "Téléchargement des librairies Forge...", 93);
    download_cjson_libraries(client_dir, installer_path, cJSON_GetObjectItem(profile, "libraries"));

    cJSON* processors = cJSON_GetObjectItem(profile, "processors");
    cJSON* data = cJSON_GetObjectItem(profile, "data");
    if (cJSON_IsArray(processors) && cJSON_GetArraySize(processors) > 0) {
        launch_report(ctx, "Installation de Forge (patchs)...", 96);
        int idx = 0, total = cJSON_GetArraySize(processors);
        cJSON* proc;
        cJSON_ArrayForEach(proc, processors) {
            idx++;
            log_msg("info", "Forge processor %d/%d\n", idx, total);
            if (!run_processor(proc, data, client_dir, installer_path, java_exe, vanilla_jar, mc_version)) {
                log_msg("error", "Forge processor %d/%d failed\n", idx, total);
                cJSON_Delete(profile);
                return 0;
            }
        }
    }

    cJSON_Delete(profile);
    return 1;
}

int forge_extract_extra_args(VmJVal* version_json, const char* kind,
                             const char** out_args, int max) {
    int n = 0;

    VmJVal* arguments = vm_get(version_json, "arguments");
    if (arguments) {
        VmJVal* arr = vm_get(arguments, kind);
        if (arr && arr->t == VM_JARR) {
            for (size_t i = 0; i < arr->a.count && n < max; i++) {
                VmJVal* item = arr->a.items[i];
                /* Feature-conditional entries (demo/resolution/...) are
                 * objects, not plain strings; skip those, they don't
                 * apply to a normal Cero launch. */
                if (item->t == VM_JSTR) out_args[n++] = item->s;
            }
        }
        return n;
    }

    if (strcmp(kind, "game") == 0) {
        const char* legacy = vm_gets(version_json, "minecraftArguments");
        if (legacy) {
            const char* p = strstr(legacy, "--tweakClass");
            if (p) {
                static char tweak_flag[] = "--tweakClass";
                static char tweak_val[256];
                p += strlen("--tweakClass");
                while (*p == ' ') p++;
                size_t i = 0;
                while (*p && *p != ' ' && i + 1 < sizeof(tweak_val)) tweak_val[i++] = *p++;
                tweak_val[i] = '\0';
                if (n + 1 < max) { out_args[n++] = tweak_flag; out_args[n++] = tweak_val; }
            }
        }
    }
    return n;
}

void forge_resolve_placeholder(const char* client_dir, const char* classpath,
                               const char* natives_dir, const char* version_name,
                               const char* raw, char* out, size_t outsz) {
    static char library_dir[MAX_PATH_SIZE];
    snprintf(library_dir, sizeof(library_dir), "%s/libraries", client_dir);

    size_t oi = 0;
    const char* p = raw;
    while (*p && oi + 1 < outsz) {
        if (p[0] == '$' && p[1] == '{') {
            const char* end = strchr(p + 2, '}');
            if (end) {
                size_t tok_len = (size_t)(end - (p + 2));
                const char* value = NULL;

                #define TOKEN_IS(lit) \
                    (tok_len == strlen(lit) && strncmp(p + 2, lit, tok_len) == 0)

                if (TOKEN_IS("classpath"))                value = classpath;
                else if (TOKEN_IS("classpath_separator"))  value = CP_SEP;
                else if (TOKEN_IS("library_directory"))    value = library_dir;
                else if (TOKEN_IS("natives_directory"))    value = natives_dir;
                else if (TOKEN_IS("version_name"))         value = version_name;
                else if (TOKEN_IS("launcher_name"))        value = "CeroClient";
                else if (TOKEN_IS("launcher_version"))     value = "1.0";

                #undef TOKEN_IS

                if (value) {
                    size_t vlen = strlen(value);
                    if (vlen > outsz - oi - 1) vlen = outsz - oi - 1;
                    memcpy(out + oi, value, vlen);
                    oi += vlen;
                    p = end + 1;
                    continue;
                }
            }
        }
        out[oi++] = *p++;
    }
    out[oi] = '\0';
}

int parse_neoforge_spec(const char* spec, char* mc, size_t mcsz,
                        char* loader, size_t loadersz) {
    loader[0] = '\0';
    if (strncmp(spec, "neoforge:", 9) != 0) return 0;
    const char* p = spec + 9;
    const char* colon = strchr(p, ':');
    if (colon) {
        size_t n = (size_t)(colon - p);
        if (n >= mcsz) n = mcsz - 1;
        memcpy(mc, p, n); mc[n] = '\0';
        snprintf(loader, loadersz, "%s", colon + 1);
    } else {
        snprintf(mc, mcsz, "%s", p);
    }
    return 1;
}

/* "1.20.4" -> "20.4", "1.21" -> "21.0" (NeoForge's version scheme mirrors
 * Minecraft's <major>.<minor>, always with a minor component). */
static void mc_to_neoforge_prefix(const char* mc_version, char* out, size_t outsz) {
    const char* p = mc_version;
    if (strncmp(p, "1.", 2) == 0) p += 2;
    if (strchr(p, '.')) {
        snprintf(out, outsz, "%s", p);
    } else {
        snprintf(out, outsz, "%s.0", p);
    }
}

/* Compares two dotted numeric version strings; returns >0 if a > b. */
static int cmp_dotted_version(const char* a, const char* b) {
    while (*a || *b) {
        long va = 0, vb = 0;
        while (*a && *a != '.') { va = va * 10 + (*a - '0'); a++; }
        while (*b && *b != '.') { vb = vb * 10 + (*b - '0'); b++; }
        if (va != vb) return (va > vb) ? 1 : -1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

int fetch_neoforge_profile(const LaunchCtx* ctx, const char* client_dir,
                          const char* mc_version, const char* neoforge_version_in,
                          const char* java_exe,
                          const char* vanilla_jar, const char* vanilla_json,
                          char* out_id, size_t out_id_sz,
                          char* out_json_path, size_t out_json_sz) {
    (void)vanilla_json;

    char neoforge_version[64];
    snprintf(neoforge_version, sizeof(neoforge_version), "%s", neoforge_version_in ? neoforge_version_in : "");

    if (!neoforge_version[0]) {
        launch_report(ctx, "Recherche de la dernière version NeoForge...", 90);

        char versions_path[MAX_PATH_SIZE];
        snprintf(versions_path, sizeof(versions_path), "%s/cache/neoforge_versions.json", client_dir);
        ensure_parent_dirs(versions_path);
        if (!download_file(NEOFORGE_VERSIONS_API, versions_path)) {
            log_msg("error", "Cannot fetch NeoForge version list\n");
            return 0;
        }
        char* txt = read_file(versions_path);
        if (!txt) return 0;
        cJSON* root = cJSON_Parse(txt);
        free(txt);
        if (!root) return 0;

        cJSON* versions = cJSON_GetObjectItem(root, "versions");
        char prefix[32];
        mc_to_neoforge_prefix(mc_version, prefix, sizeof(prefix));
        char match_prefix[40];
        snprintf(match_prefix, sizeof(match_prefix), "%s.", prefix);

        char best[64] = "";
        cJSON* v;
        cJSON_ArrayForEach(v, versions) {
            if (!cJSON_IsString(v)) continue;
            if (strncmp(v->valuestring, match_prefix, strlen(match_prefix)) != 0) continue;
            if (!best[0] || cmp_dotted_version(v->valuestring, best) > 0) {
                snprintf(best, sizeof(best), "%s", v->valuestring);
            }
        }
        cJSON_Delete(root);

        if (!best[0]) {
            log_msg("error", "No NeoForge build found for Minecraft %s (prefix %s)\n", mc_version, prefix);
            return 0;
        }
        snprintf(neoforge_version, sizeof(neoforge_version), "%s", best);
        log_msg("info", "Using NeoForge version: %s\n", neoforge_version);
    }

    char installer_url[512];
    snprintf(installer_url, sizeof(installer_url),
            "%snet/neoforged/neoforge/%s/neoforge-%s-installer.jar",
            NEOFORGE_MAVEN, neoforge_version, neoforge_version);

    char installer_path[MAX_PATH_SIZE];
    snprintf(installer_path, sizeof(installer_path),
            "%s/neoforge/installers/neoforge-%s-installer.jar", client_dir, neoforge_version);

    if (!file_exists(installer_path)) {
        launch_report(ctx, "Téléchargement de l'installateur NeoForge...", 91);
        ensure_parent_dirs(installer_path);
        if (!download_file(installer_url, installer_path)) {
            log_msg("error", "Cannot download NeoForge installer from %s\n", installer_url);
            return 0;
        }
    }

    size_t profile_sz = 0;
    char* profile_txt = zip_extract_to_mem(installer_path, "install_profile.json", &profile_sz);
    if (!profile_txt) {
        log_msg("error", "install_profile.json not found in %s\n", installer_path);
        return 0;
    }
    cJSON* profile = cJSON_Parse(profile_txt);
    free(profile_txt);
    if (!profile) return 0;

    /* NeoForge only ever ships the modern processors-based profile
     * (it doesn't predate that format), so no legacy branch needed here. */
    cJSON* install_section = cJSON_GetObjectItem(profile, "install");
    cJSON* json_field = cJSON_GetObjectItem(profile, "json");
    if (!json_field && install_section) json_field = cJSON_GetObjectItem(install_section, "json");
    const char* version_json_entry = cJSON_IsString(json_field) ? json_field->valuestring : "/version.json";

    cJSON* version_field = cJSON_GetObjectItem(profile, "version");
    if (cJSON_IsString(version_field)) {
        snprintf(out_id, out_id_sz, "%s", version_field->valuestring);
    } else {
        snprintf(out_id, out_id_sz, "neoforge-%s", neoforge_version);
    }

    snprintf(out_json_path, out_json_sz, "%s/versions/%s/%s.json", client_dir, out_id, out_id);
    if (!zip_extract_entry(installer_path, version_json_entry, out_json_path)) {
        cJSON_Delete(profile);
        return 0;
    }

    launch_report(ctx, "Téléchargement des librairies NeoForge...", 93);
    download_cjson_libraries(client_dir, installer_path, cJSON_GetObjectItem(profile, "libraries"));

    cJSON* processors = cJSON_GetObjectItem(profile, "processors");
    cJSON* data = cJSON_GetObjectItem(profile, "data");
    if (cJSON_IsArray(processors) && cJSON_GetArraySize(processors) > 0) {
        launch_report(ctx, "Installation de NeoForge (patchs)...", 96);
        int idx = 0, total = cJSON_GetArraySize(processors);
        cJSON* proc;
        cJSON_ArrayForEach(proc, processors) {
            idx++;
            log_msg("info", "NeoForge processor %d/%d\n", idx, total);
            if (!run_processor(proc, data, client_dir, installer_path, java_exe, vanilla_jar, mc_version)) {
                log_msg("error", "NeoForge processor %d/%d failed\n", idx, total);
                cJSON_Delete(profile);
                return 0;
            }
        }
    }

    cJSON_Delete(profile);
    return 1;
}