#include "../../include/launch/launch_minecraft.h"
#include "../../include/launch/launch_ctx.h"
#include "../../include/launch/fabric.h"
#include "../../include/launch/forge.h"
#include "../../include/launch/quilt.h"
#include "../../include/instances/instance.h"
#include "../../include/launch/account.h"
#include "../../include/launch/version_step.h"
#include "../../include/launch/cero_agent.h"
#include "../../include/launch/deobfuscation_step.h"
#include "../../include/launch/java_step.h"
#include "../../include/launch/classpath.h"
#include "../../include/launch/jvm_args.h"
#include "../../include/launch/process_step.h"
#include "../../include/config/config.h"
#include "../../include/ipc/settings_handlers.h"
#include "../../include/utils/version_manifest.h"
#include "../../include/utils/libraries.h"
#include "../../include/utils/assets.h"
#include "../../include/core/logger.h"
#include <stdio.h>
#include <string.h>

void launch_minecraft(const char* version,
                      launch_progress_cb cb, void* userdata) {
    LaunchCtx ctx;
    ctx.cb = cb;
    ctx.userdata = userdata;

    char client_dir[MAX_PATH_SIZE];
    snprintf(client_dir, sizeof(client_dir), "%s", client_path);

    char mc_version[64] = "";
    char fabric_loader[64] = "";
    int is_fabric = parse_fabric_spec(version, mc_version, sizeof(mc_version),
                                      fabric_loader, sizeof(fabric_loader));
    const char* vanilla_version = is_fabric ? mc_version : version;

    char username[128];
    char uuid[64];
    char token[2048];
    if (!resolve_account(&ctx, client_dir, username, sizeof(username),
                         uuid, sizeof(uuid), token, sizeof(token))) {
        return;
    }

    char json_dest[MAX_PATH_SIZE];
    char jar_dest[MAX_PATH_SIZE];
    if (!resolve_version_json(&ctx, client_dir, vanilla_version,
                              json_dest, sizeof(json_dest),
                              jar_dest, sizeof(jar_dest))) {
        return;
    }

    char cero_jar_path[MAX_PATH_SIZE];
    int has_cero = extract_cero_agent(client_dir, cero_jar_path, sizeof(cero_jar_path));

    char mapped_jar[MAX_PATH_SIZE];
    if (!resolve_deobfuscated_jar(&ctx, client_dir, vanilla_version,
                                  jar_dest, json_dest, mapped_jar, sizeof(mapped_jar))) {
        return;
    }

    launch_report(&ctx, "Téléchargement des librairies...", 42);
    download_libraries(client_dir, vanilla_version);

    launch_report(&ctx, "Extraction des natives...", 55);
    extract_all_natives(client_dir, vanilla_version);

    launch_report(&ctx, "Téléchargement des assets...", 68);
    download_assets(client_dir, vanilla_version);

    char java_exe[MAX_PATH_SIZE];
    if (!resolve_java_runtime(&ctx, client_dir, vanilla_version, java_exe, sizeof(java_exe))) {
        return;
    }

    launch_report(&ctx, "Préparation du lancement...", 90);
    VmJVal* root = vm_load_json(json_dest);
    if (!root) {
        launch_report(&ctx, "Erreur : lecture version JSON !", -1);
        log_msg("error", "Cannot read version json for launch\n");
        return;
    }

    VmJVal* fabric_root = NULL;
    char fabric_id[128] = "";
    char fabric_main[128] = "";

    if (is_fabric) {
        launch_report(&ctx, "Téléchargement du profil Fabric...", 92);
        char fabric_json_path[MAX_PATH_SIZE];
        if (!fetch_fabric_profile(client_dir, mc_version, fabric_loader,
                                  fabric_id, sizeof(fabric_id),
                                  fabric_json_path, sizeof(fabric_json_path))) {
            launch_report(&ctx, "Erreur : profil Fabric introuvable !", -1);
            log_msg("error", "Cannot fetch Fabric profile for %s\n", mc_version);
            vm_free(root);
            return;
        }
        fabric_root = vm_load_json(fabric_json_path);
        if (!fabric_root) {
            launch_report(&ctx, "Erreur : lecture profil Fabric !", -1);
            vm_free(root);
            return;
        }
        const char* fmc = vm_gets(fabric_root, "mainClass");
        if (fmc) snprintf(fabric_main, sizeof(fabric_main), "%s", fmc);
    }

    const char* game_main_class = (fabric_main[0]) ? fabric_main
                                                   : vm_gets(root, "mainClass");
    const char* asset_index = NULL;
    VmJVal* ai = vm_get(root, "assetIndex");
    if (ai) asset_index = vm_gets(ai, "id");
    if (!asset_index) asset_index = vm_gets(root, "assets");
    if (!asset_index) asset_index = "legacy";

    if (!game_main_class) {
        launch_report(&ctx, "Erreur : mainClass manquant !", -1);
        log_msg("error", "No mainClass in version json\n");
        if (fabric_root) vm_free(fabric_root);
        vm_free(root);
        return;
    }

    static CpLib libs_arr[512];
    int libs_count = 0;

    if (fabric_root) {
        launch_report(&ctx, "Téléchargement des librairies Fabric...", 95);
        collect_fabric_libs(client_dir, fabric_root, libs_arr, &libs_count, 512);
    }
    collect_vanilla_libs(client_dir, root, libs_arr, &libs_count, 512);

    download_pending_libraries(libs_arr, libs_count);

    char client_jar[MAX_PATH_SIZE];
    if (mapped_jar[0]) {
        snprintf(client_jar, sizeof(client_jar), "%s", mapped_jar);
    } else {
        snprintf(client_jar, sizeof(client_jar),
                 "%s/versions/%s/%s.jar", client_dir, vanilla_version, vanilla_version);
    }

    static char classpath[65536];
    build_classpath(classpath, sizeof(classpath), has_cero, cero_jar_path,
                    libs_arr, libs_count, client_jar);

    const char* version_id_for_args = is_fabric ? fabric_id : vanilla_version;

    LaunchParams lp;
    lp.java_exe = java_exe;
    lp.classpath = classpath;
    lp.game_main_class = game_main_class;
    lp.has_cero = has_cero;
    lp.username = username;
    lp.version_id_for_args = version_id_for_args;
    lp.client_dir = client_dir;
    lp.asset_index = asset_index;
    lp.uuid = uuid;
    lp.token = token;
    lp.vanilla_version = vanilla_version;
    lp.bridge_port = local_bridge_port;
    lp.ram_mb = get_configured_ram_mb();
    lp.macos_first_thread = (vm_get(root, "arguments") != NULL);
    lp.extra_jvm_args = NULL;
    lp.extra_jvm_count = 0;
    lp.extra_game_args = NULL;
    lp.extra_game_count = 0;

    const char* argv[192];
    char bridge_port_str[16];
    build_launch_argv(&lp, argv, 192, bridge_port_str, sizeof(bridge_port_str));

    LaunchUserdata* ud = (LaunchUserdata*)userdata;
    run_game_process(&ctx, ud, java_exe, argv, version, username, is_fabric);

    if (fabric_root) vm_free(fabric_root);
    vm_free(root);
}

int launch_instance(const char* instance_id, launch_progress_cb cb, void* userdata) {
    LaunchCtx ctx;
    ctx.cb = cb;
    ctx.userdata = userdata;

    Instance inst;
    if (!instance_get(instance_id, &inst)) {
        launch_report(&ctx, "Erreur : instance introuvable !", -1);
        log_msg("error", "launch_instance: unknown instance '%s'\n", instance_id);
        return 0;
    }

    char instance_dir[MAX_PATH_SIZE];
    instance_get_dir(inst.id, instance_dir, sizeof(instance_dir));

    const char* mc_version = inst.mc_version;
    int is_fabric = (inst.loader == LOADER_FABRIC);
    int is_forge  = (inst.loader == LOADER_FORGE);
    int is_neoforge = (inst.loader == LOADER_NEOFORGE);
    int is_quilt = (inst.loader == LOADER_QUILT);

    log_msg("info", "Launching instance '%s' (%s %s%s%s)\n", inst.name, mc_version,
            loader_type_to_str(inst.loader),
            inst.loader_version[0] ? " " : "", inst.loader_version);

    char username[128];
    char uuid[64];
    char token[2048];
    if (!resolve_account(&ctx, client_path, username, sizeof(username),
                         uuid, sizeof(uuid), token, sizeof(token))) {
        return 0;
    }

    char json_dest[MAX_PATH_SIZE];
    char jar_dest[MAX_PATH_SIZE];
    if (!resolve_version_json(&ctx, instance_dir, mc_version,
                              json_dest, sizeof(json_dest), jar_dest, sizeof(jar_dest))) {
        return 0;
    }

    const int has_cero = 0;
    const char* cero_jar_path = "";

    char mapped_jar[MAX_PATH_SIZE];
    mapped_jar[0] = '\0';

    char java_exe[MAX_PATH_SIZE];
    if (!resolve_java_runtime(&ctx, instance_dir, mc_version, java_exe, sizeof(java_exe))) {
        return 0;
    }

    VmJVal* root = vm_load_json(json_dest);
    if (!root) {
        launch_report(&ctx, "Erreur : lecture version JSON !", -1);
        log_msg("error", "Cannot read version json for instance launch\n");
        return 0;
    }

    VmJVal* loader_root = NULL;
    char loader_id[128] = "";
    char loader_main[160] = "";

    if (is_fabric) {
        launch_report(&ctx, "Téléchargement du profil Fabric...", 92);
        char fabric_json_path[MAX_PATH_SIZE];
        if (!fetch_fabric_profile(instance_dir, mc_version, inst.loader_version,
                                  loader_id, sizeof(loader_id),
                                  fabric_json_path, sizeof(fabric_json_path))) {
            launch_report(&ctx, "Erreur : profil Fabric introuvable !", -1);
            vm_free(root);
            return 0;
        }
        loader_root = vm_load_json(fabric_json_path);
        if (!loader_root) {
            launch_report(&ctx, "Erreur : lecture profil Fabric !", -1);
            vm_free(root);
            return 0;
        }
        const char* mc_class = vm_gets(loader_root, "mainClass");
        if (mc_class) snprintf(loader_main, sizeof(loader_main), "%s", mc_class);
    } else if (is_forge) {
        launch_report(&ctx, "Installation de Forge...", 91);
        char forge_json_path[MAX_PATH_SIZE];
        if (!fetch_forge_profile(&ctx, instance_dir, mc_version, inst.loader_version,
                                 java_exe, jar_dest, json_dest,
                                 loader_id, sizeof(loader_id),
                                 forge_json_path, sizeof(forge_json_path))) {
            launch_report(&ctx, "Erreur : installation Forge échouée !", -1);
            vm_free(root);
            return 0;
        }
        loader_root = vm_load_json(forge_json_path);
        if (!loader_root) {
            launch_report(&ctx, "Erreur : lecture profil Forge !", -1);
            vm_free(root);
            return 0;
        }
        const char* mc_class = vm_gets(loader_root, "mainClass");
        if (mc_class) snprintf(loader_main, sizeof(loader_main), "%s", mc_class);
    } else if (is_quilt) {
        launch_report(&ctx, "Téléchargement du profil Quilt...", 92);
        char quilt_json_path[MAX_PATH_SIZE];
        if (!fetch_quilt_profile(instance_dir, mc_version, inst.loader_version,
                                 loader_id, sizeof(loader_id),
                                 quilt_json_path, sizeof(quilt_json_path))) {
            launch_report(&ctx, "Erreur : profil Quilt introuvable !", -1);
            vm_free(root);
            return 0;
        }
        loader_root = vm_load_json(quilt_json_path);
        if (!loader_root) {
            launch_report(&ctx, "Erreur : lecture profil Quilt !", -1);
            vm_free(root);
            return 0;
        }
        const char* mc_class = vm_gets(loader_root, "mainClass");
        if (mc_class) snprintf(loader_main, sizeof(loader_main), "%s", mc_class);
    } else if (is_neoforge) {
        launch_report(&ctx, "Installation de NeoForge...", 91);
        char neoforge_json_path[MAX_PATH_SIZE];
        if (!fetch_neoforge_profile(&ctx, instance_dir, mc_version, inst.loader_version,
                                    java_exe, jar_dest, json_dest,
                                    loader_id, sizeof(loader_id),
                                    neoforge_json_path, sizeof(neoforge_json_path))) {
            launch_report(&ctx, "Erreur : installation NeoForge échouée !", -1);
            vm_free(root);
            return 0;
        }
        loader_root = vm_load_json(neoforge_json_path);
        if (!loader_root) {
            launch_report(&ctx, "Erreur : lecture profil NeoForge !", -1);
            vm_free(root);
            return 0;
        }
        const char* mc_class = vm_gets(loader_root, "mainClass");
        if (mc_class) snprintf(loader_main, sizeof(loader_main), "%s", mc_class);
    }

    const char* game_main_class = loader_main[0] ? loader_main : vm_gets(root, "mainClass");
    const char* asset_index = NULL;
    VmJVal* ai = vm_get(root, "assetIndex");
    if (ai) asset_index = vm_gets(ai, "id");
    if (!asset_index) asset_index = vm_gets(root, "assets");
    if (!asset_index) asset_index = "legacy";

    if (!game_main_class) {
        launch_report(&ctx, "Erreur : mainClass manquant !", -1);
        if (loader_root) vm_free(loader_root);
        vm_free(root);
        return 0;
    }

    launch_report(&ctx, "Téléchargement des librairies...", 94);
    download_libraries(instance_dir, mc_version);

    launch_report(&ctx, "Extraction des natives...", 96);
    extract_all_natives(instance_dir, mc_version);

    launch_report(&ctx, "Téléchargement des assets...", 98);
    download_assets(instance_dir, mc_version);

    static CpLib libs_arr[512];
    int libs_count = 0;

    if (loader_root) {
        if (is_forge || is_neoforge) {
            collect_forge_libs(instance_dir, loader_root, libs_arr, &libs_count, 512);
        } else {
            collect_fabric_libs(instance_dir, loader_root, libs_arr, &libs_count, 512);
        }
    }
    collect_vanilla_libs(instance_dir, root, libs_arr, &libs_count, 512);

    download_pending_libraries(libs_arr, libs_count);

    char client_jar[MAX_PATH_SIZE];
    if (mapped_jar[0]) {
        snprintf(client_jar, sizeof(client_jar), "%s", mapped_jar);
    } else {
        snprintf(client_jar, sizeof(client_jar),
                "%s/versions/%s/%s.jar", instance_dir, mc_version, mc_version);
    }

    static char classpath[65536];
    build_classpath(classpath, sizeof(classpath), has_cero, cero_jar_path,
                    libs_arr, libs_count, client_jar);

    const char* version_id_for_args = loader_id[0] ? loader_id : mc_version;

    static const char* extra_jvm[16];
    static const char* extra_game[32];
    int extra_jvm_n = 0, extra_game_n = 0;
    if ((is_forge || is_neoforge) && loader_root) {
        static const char* raw_jvm[16];
        static const char* raw_game[32];
        int raw_jvm_n  = forge_extract_extra_args(loader_root, "jvm",  raw_jvm,  16);
        int raw_game_n = forge_extract_extra_args(loader_root, "game", raw_game, 32);

        static char jvm_buf[16][MAX_PATH_SIZE];
        static char game_buf[32][MAX_PATH_SIZE];

        char natives_dir[MAX_PATH_SIZE];
        snprintf(natives_dir, sizeof(natives_dir), "%s/versions/%s/natives", instance_dir, mc_version);

        char jar_version_name[256];
        {
            const char* base = strrchr(client_jar, '/');
#ifdef _WIN32
            const char* bs = strrchr(client_jar, '\\');
            if (bs && (!base || bs > base)) base = bs;
#endif
            base = base ? base + 1 : client_jar;
            snprintf(jar_version_name, sizeof(jar_version_name), "%s", base);
            size_t bl = strlen(jar_version_name);
            if (bl > 4 && strcmp(jar_version_name + bl - 4, ".jar") == 0)
                jar_version_name[bl - 4] = '\0';
        }

        for (int i = 0; i < raw_jvm_n; i++) {
            forge_resolve_placeholder(instance_dir, classpath, natives_dir, jar_version_name,
                                      raw_jvm[i], jvm_buf[i], sizeof(jvm_buf[i]));
            extra_jvm[i] = jvm_buf[i];
        }
        extra_jvm_n = raw_jvm_n;

        for (int i = 0; i < raw_game_n; i++) {
            forge_resolve_placeholder(instance_dir, classpath, natives_dir, version_id_for_args,
                                      raw_game[i], game_buf[i], sizeof(game_buf[i]));
            extra_game[i] = game_buf[i];
        }
        extra_game_n = raw_game_n;
    }

    LaunchParams lp;
    lp.java_exe = java_exe;
    lp.classpath = classpath;
    lp.game_main_class = game_main_class;
    lp.has_cero = has_cero;
    lp.username = username;
    lp.version_id_for_args = version_id_for_args;
    lp.client_dir = instance_dir;
    lp.asset_index = asset_index;
    lp.uuid = uuid;
    lp.token = token;
    lp.vanilla_version = mc_version;
    lp.bridge_port = local_bridge_port;
    lp.ram_mb = (inst.ram_mb > 0) ? inst.ram_mb : get_configured_ram_mb();
    lp.macos_first_thread = (vm_get(root, "arguments") != NULL);
    lp.extra_jvm_args = extra_jvm_n ? extra_jvm : NULL;
    lp.extra_jvm_count = extra_jvm_n;
    lp.extra_game_args = extra_game_n ? extra_game : NULL;
    lp.extra_game_count = extra_game_n;

    const char* argv[192];
    char bridge_port_str[16];
    build_launch_argv(&lp, argv, 192, bridge_port_str, sizeof(bridge_port_str));

    instance_touch_last_played(inst.id);

    LaunchUserdata* ud = (LaunchUserdata*)userdata;
    run_game_process(&ctx, ud, java_exe, argv, mc_version, username, is_fabric);

    if (loader_root) vm_free(loader_root);
    vm_free(root);
    return 1;
}