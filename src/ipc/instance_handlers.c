#include "../../include/platform/platform_defines.h"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
#endif

#include "../../include/ipc/instance_handlers.h"
#include "../../include/instances/instance.h"
#include "../../include/launch/launch_minecraft.h"
#include "../../include/launch/game_registry.h"
#include "../../include/launch/manifest.h"
#include "../../include/app/app_state.h"
#include "../../include/config/config.h"
#include "../../include/net/download.h"
#include "../../include/utils/file_utils.h"
#include "../../include/vendor/cJSON.h"
#include "../../include/ui/ui.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORGE_PROMOS_URL "https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json"
#define FABRIC_META      "https://meta.fabricmc.net/v2"
#define NEOFORGE_VERSIONS_API "https://maven.neoforged.net/api/maven/versions/releases/net/neoforged/neoforge"
#define QUILT_META       "https://meta.quiltmc.org/v3"

static char* read_whole_file_ih(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz < 0 || sz > 16 * 1024 * 1024) { fclose(f); return NULL; }
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* webview bind() calls us with req = a JSON array of the JS call's
 * arguments, e.g. on_create_instance("id", "[\"My pack\",\"1.20.1\",...]", ui) */
static const char* arr_get_str(cJSON* arr, int idx, char* out, size_t outsz) {
    cJSON* v = cJSON_GetArrayItem(arr, idx);
    if (!cJSON_IsString(v)) { out[0] = '\0'; return out; }
    snprintf(out, outsz, "%s", v->valuestring);
    return out;
}

static long arr_get_num(cJSON* arr, int idx, long def) {
    cJSON* v = cJSON_GetArrayItem(arr, idx);
    return cJSON_IsNumber(v) ? (long)v->valuedouble : def;
}

void on_list_instances(const char* id, const char* req, void* arg) {
    (void)req;
    instances_init();
    char buf[64 * 1024];
    if (!instances_list_json(buf, sizeof(buf))) {
        ui_return(arg, id, 0, "[]");
        return;
    }
    ui_return(arg, id, 0, buf);
}

void on_create_instance(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    if (!args) { ui_return(arg, id, 1, "\"invalid_json\""); return; }

    char name[128], mc_version[64], loader_str[32], loader_version[64];
    arr_get_str(args, 0, name, sizeof(name));
    arr_get_str(args, 1, mc_version, sizeof(mc_version));
    arr_get_str(args, 2, loader_str, sizeof(loader_str));
    arr_get_str(args, 3, loader_version, sizeof(loader_version));
    cJSON_Delete(args);

    if (!name[0] || !mc_version[0]) {
        ui_return(arg, id, 1, "\"missing_fields\"");
        return;
    }

    instances_init();
    LoaderType loader = loader_type_from_str(loader_str);

    char new_id[64];
    if (!instance_create(name, mc_version, loader, loader_version, new_id, sizeof(new_id))) {
        ui_return(arg, id, 1, "\"create_failed\"");
        return;
    }

    char result[96];
    snprintf(result, sizeof(result), "\"%s\"", new_id);
    ui_return(arg, id, 0, result);
}

void on_delete_instance(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char inst_id[64];
    arr_get_str(args, 0, inst_id, sizeof(inst_id));
    if (args) cJSON_Delete(args);

    if (!inst_id[0]) { ui_return(arg, id, 1, "\"no_id\""); return; }
    int ok = instance_delete(inst_id);
    ui_return(arg, id, ok ? 0 : 1, ok ? "\"ok\"" : "\"not_found\"");
}

void on_rename_instance(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char inst_id[64], new_name[128];
    arr_get_str(args, 0, inst_id, sizeof(inst_id));
    arr_get_str(args, 1, new_name, sizeof(new_name));
    if (args) cJSON_Delete(args);

    if (!inst_id[0] || !new_name[0]) { ui_return(arg, id, 1, "\"missing_fields\""); return; }
    int ok = instance_rename(inst_id, new_name);
    ui_return(arg, id, ok ? 0 : 1, ok ? "\"ok\"" : "\"not_found\"");
}

void on_set_instance_ram(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char inst_id[64];
    arr_get_str(args, 0, inst_id, sizeof(inst_id));
    long ram_mb = arr_get_num(args, 1, 0);
    if (args) cJSON_Delete(args);

    if (!inst_id[0]) { ui_return(arg, id, 1, "\"no_id\""); return; }
    int ok = instance_set_ram(inst_id, ram_mb);
    ui_return(arg, id, ok ? 0 : 1, ok ? "\"ok\"" : "\"not_found\"");
}

typedef struct {
    char     instance_id[64];
    void*    ui;
    unsigned gen;
} InstanceLaunchArgs;

static void js_safe_key(const char* in, char* out, size_t cap) {
    size_t n = 0;
    for (const char* p = in; *p && n + 1 < cap; p++) {
        char c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        out[n++] = ok ? c : '_';
    }
    out[n] = '\0';
}

static void instance_launch_progress(const char* step, int pct, void* userdata) {
    LaunchUserdata* ud = (LaunchUserdata*)userdata;
    void* w = ud ? ud->ui : NULL;
    if (!w || !ud->key) return;

    char key[64];
    js_safe_key(ud->key, key, sizeof(key));

    char step_safe[256];
    size_t si = 0, di = 0;
    while (step[si] && di + 2 < sizeof(step_safe)) {
        if (step[si] == '"' || step[si] == '\\') step_safe[di++] = '\\';
        step_safe[di++] = step[si++];
    }
    step_safe[di] = '\0';

    char js[640];
    if (pct < 0) {
        snprintf(js, sizeof(js),
                 "if(window.onInstanceError) onInstanceError(\"%s\",\"%s\")", key, step_safe);
    } else {
        snprintf(js, sizeof(js),
                 "if(window.onInstanceProgress) onInstanceProgress(\"%s\",\"%s\",%d)", key, step_safe, pct);
    }
    ui_eval(w, js);
}

#ifdef _WIN32
static DWORD WINAPI instance_launch_thread(LPVOID arg) {
#else
static void* instance_launch_thread(void* arg) {
#endif
    InstanceLaunchArgs* la = (InstanceLaunchArgs*)arg;

    LaunchUserdata ud = { la->ui, NULL, la->instance_id, la->gen };
    launch_instance(la->instance_id, instance_launch_progress, &ud);

    game_registry_finish(la->instance_id, la->gen);

    char key[64];
    js_safe_key(la->instance_id, key, sizeof(key));
    char js[160];
    snprintf(js, sizeof(js), "if(window.onInstanceDone) onInstanceDone(\"%s\")", key);
    ui_eval(la->ui, js);

    free(la);
    return 0;
}

void on_launch_instance(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char inst_id[64];
    arr_get_str(args, 0, inst_id, sizeof(inst_id));
    if (args) cJSON_Delete(args);

    if (!inst_id[0]) { ui_return(arg, id, 1, "\"no_id\""); return; }

    unsigned gen = game_registry_begin(inst_id);
    if (!gen) {
        ui_return(arg, id, 1, "\"already_running\"");
        return;
    }

    InstanceLaunchArgs* la = malloc(sizeof(InstanceLaunchArgs));
    if (!la) {
        game_registry_finish(inst_id, gen);
        ui_return(arg, id, 1, "\"alloc_error\"");
        return;
    }
    snprintf(la->instance_id, sizeof(la->instance_id), "%s", inst_id);
    la->ui  = arg;
    la->gen = gen;

#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, instance_launch_thread, la, 0, NULL);
    if (h) CloseHandle(h);
    else {
        game_registry_finish(inst_id, gen);
        free(la);
        ui_return(arg, id, 1, "\"thread_error\"");
        return;
    }
#else
    pthread_t t;
    if (pthread_create(&t, NULL, instance_launch_thread, la) != 0) {
        game_registry_finish(inst_id, gen);
        free(la);
        ui_return(arg, id, 1, "\"thread_error\"");
        return;
    }
    pthread_detach(t);
#endif

    ui_return(arg, id, 0, "\"ok\"");
}

void on_kill_instance(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char inst_id[64];
    arr_get_str(args, 0, inst_id, sizeof(inst_id));
    if (args) cJSON_Delete(args);

    if (!inst_id[0]) { ui_return(arg, id, 1, "\"no_id\""); return; }

    int hit = game_registry_kill(inst_id);
    ui_return(arg, id, hit ? 0 : 1, hit ? "\"ok\"" : "\"not_running\"");
}

void on_running_games(const char* id, const char* req, void* arg) {
    (void)req;
    char buf[2048];
    game_registry_running_json(buf, sizeof(buf));
    ui_return(arg, id, 0, buf);
}

/* ---- version listing (vanilla / fabric / forge) ---- */

void on_get_mc_versions(const char* id, const char* req, void* arg) {
    (void)req;
    download_manifest();

    char path[MAX_PATH_SIZE];
    snprintf(path, sizeof(path), "%s/version_manifest.json", client_path);

    char* txt = read_whole_file_ih(path);
    if (!txt) { ui_return(arg, id, 1, "\"manifest_unavailable\""); return; }

    ui_return(arg, id, 0, txt);
    free(txt);
}

void on_get_forge_versions(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char mc_version[64];
    arr_get_str(args, 0, mc_version, sizeof(mc_version));
    if (args) cJSON_Delete(args);

    if (!mc_version[0]) { ui_return(arg, id, 1, "\"no_version\""); return; }

    char promos_path[MAX_PATH_SIZE];
    snprintf(promos_path, sizeof(promos_path), "%s/cache/forge_promotions.json", client_path);
    ensure_parent_dirs(promos_path);
    if (!download_file(FORGE_PROMOS_URL, promos_path)) {
        ui_return(arg, id, 1, "\"fetch_failed\"");
        return;
    }

    char* txt = read_whole_file_ih(promos_path);
    if (!txt) { ui_return(arg, id, 1, "\"read_failed\""); return; }
    cJSON* root = cJSON_Parse(txt);
    free(txt);
    if (!root) { ui_return(arg, id, 1, "\"parse_failed\""); return; }

    cJSON* promos = cJSON_GetObjectItem(root, "promos");
    cJSON* out = cJSON_CreateArray();

    char prefix[80];
    snprintf(prefix, sizeof(prefix), "%s-", mc_version);
    size_t prefix_len = strlen(prefix);

    cJSON* item;
    cJSON_ArrayForEach(item, promos) {
        if (strncmp(item->string, prefix, prefix_len) != 0) continue;
        if (!cJSON_IsString(item)) continue;

        cJSON* entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "version", item->valuestring);
        const char* suffix = item->string + prefix_len;
        cJSON_AddBoolToObject(entry, "recommended", strcmp(suffix, "recommended") == 0);
        cJSON_AddItemToArray(out, entry);
    }

    char* out_txt = cJSON_PrintUnformatted(out);
    ui_return(arg, id, 0, out_txt ? out_txt : "[]");
    free(out_txt);
    cJSON_Delete(out);
    cJSON_Delete(root);
}

void on_get_fabric_versions(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char mc_version[64];
    arr_get_str(args, 0, mc_version, sizeof(mc_version));
    if (args) cJSON_Delete(args);

    if (!mc_version[0]) { ui_return(arg, id, 1, "\"no_version\""); return; }

    char url[512];
    snprintf(url, sizeof(url), "%s/versions/loader/%s", FABRIC_META, mc_version);

    char cache_path[MAX_PATH_SIZE];
    snprintf(cache_path, sizeof(cache_path), "%s/cache/fabric_loaders_%s.json", client_path, mc_version);
    ensure_parent_dirs(cache_path);

    if (!download_file(url, cache_path)) {
        ui_return(arg, id, 1, "\"fetch_failed\"");
        return;
    }

    char* txt = read_whole_file_ih(cache_path);
    if (!txt) { ui_return(arg, id, 1, "\"read_failed\""); return; }

    ui_return(arg, id, 0, txt);
    free(txt);
}

/* ---- NeoForge (own maven, own version list — no "recommended" promos) ---- */

static void mc_to_neoforge_prefix_ih(const char* mc_version, char* out, size_t outsz) {
    const char* p = mc_version;
    if (strncmp(p, "1.", 2) == 0) p += 2;
    if (strchr(p, '.')) {
        snprintf(out, outsz, "%s", p);
    } else {
        snprintf(out, outsz, "%s.0", p);
    }
}

static int cmp_dotted_version_ih(const char* a, const char* b) {
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

void on_get_neoforge_versions(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char mc_version[64];
    arr_get_str(args, 0, mc_version, sizeof(mc_version));
    if (args) cJSON_Delete(args);

    if (!mc_version[0]) { ui_return(arg, id, 1, "\"no_version\""); return; }

    char cache_path[MAX_PATH_SIZE];
    snprintf(cache_path, sizeof(cache_path), "%s/cache/neoforge_versions.json", client_path);
    ensure_parent_dirs(cache_path);
    if (!download_file(NEOFORGE_VERSIONS_API, cache_path)) {
        ui_return(arg, id, 1, "\"fetch_failed\"");
        return;
    }

    char* txt = read_whole_file_ih(cache_path);
    if (!txt) { ui_return(arg, id, 1, "\"read_failed\""); return; }
    cJSON* root = cJSON_Parse(txt);
    free(txt);
    if (!root) { ui_return(arg, id, 1, "\"parse_failed\""); return; }

    char prefix[32];
    mc_to_neoforge_prefix_ih(mc_version, prefix, sizeof(prefix));
    char match_prefix[40];
    snprintf(match_prefix, sizeof(match_prefix), "%s.", prefix);

    cJSON* versions = cJSON_GetObjectItem(root, "versions");
    cJSON* out = cJSON_CreateArray();
    char latest[64] = "";

    cJSON* v;
    cJSON_ArrayForEach(v, versions) {
        if (!cJSON_IsString(v)) continue;
        if (strncmp(v->valuestring, match_prefix, strlen(match_prefix)) != 0) continue;
        cJSON_AddItemToArray(out, cJSON_CreateString(v->valuestring));
        if (!latest[0] || cmp_dotted_version_ih(v->valuestring, latest) > 0) {
            snprintf(latest, sizeof(latest), "%s", v->valuestring);
        }
    }
    cJSON_Delete(root);

    cJSON* result = cJSON_CreateObject();
    cJSON_AddItemToObject(result, "versions", out);
    cJSON_AddStringToObject(result, "latest", latest);

    char* out_txt = cJSON_PrintUnformatted(result);
    ui_return(arg, id, 0, out_txt ? out_txt : "{\"versions\":[],\"latest\":\"\"}");
    free(out_txt);
    cJSON_Delete(result);
}

void on_get_quilt_versions(const char* id, const char* req, void* arg) {
    cJSON* args = cJSON_Parse(req);
    char mc_version[64];
    arr_get_str(args, 0, mc_version, sizeof(mc_version));
    if (args) cJSON_Delete(args);

    if (!mc_version[0]) { ui_return(arg, id, 1, "\"no_version\""); return; }

    char url[512];
    snprintf(url, sizeof(url), "%s/versions/loader/%s", QUILT_META, mc_version);

    char cache_path[MAX_PATH_SIZE];
    snprintf(cache_path, sizeof(cache_path), "%s/cache/quilt_loaders_%s.json", client_path, mc_version);
    ensure_parent_dirs(cache_path);

    if (!download_file(url, cache_path)) {
        ui_return(arg, id, 1, "\"fetch_failed\"");
        return;
    }

    char* txt = read_whole_file_ih(cache_path);
    if (!txt) { ui_return(arg, id, 1, "\"read_failed\""); return; }

    ui_return(arg, id, 0, txt);
    free(txt);
}