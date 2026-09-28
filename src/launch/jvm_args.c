#include "../../include/launch/jvm_args.h"
#include "../../include/config/config.h"
#include "../../include/core/logger.h"
#include <stdio.h>

#define CLIENT_BRAND "CeroClient"

void build_classpath(char* out, size_t outsz,
                     int has_cero, const char* cero_jar_path,
                     const CpLib* libs, int libs_count,
                     const char* client_jar) {
    out[0] = '\0';
    size_t cplen = 0;

    if (has_cero) {
        int wn = snprintf(out + cplen, outsz - cplen, "%s%s", cero_jar_path, CP_SEP);
        if (wn > 0 && (size_t)wn < outsz - cplen) {
            cplen += (size_t)wn;
        }
    }

    for (int i = 0; i < libs_count; i++) {
        if (cplen >= outsz) break;
        int wn = snprintf(out + cplen, outsz - cplen, "%s%s", libs[i].path, CP_SEP);
        if (wn > 0) {
            if ((size_t)wn >= outsz - cplen) {
                log_msg("error", "Classpath buffer overflow!\n");
                cplen = outsz - 1;
                break;
            }
            cplen += (size_t)wn;
        }
    }

    snprintf(out + cplen, outsz - cplen, "%s", client_jar);
}

int build_launch_argv(const LaunchParams* p, const char** argv, int max_argv,
                      char* bridge_port_buf, size_t bridge_port_buf_sz) {
    (void)max_argv; 

    static char natives_dir[MAX_PATH_SIZE];
    snprintf(natives_dir, sizeof(natives_dir),
             "%s/versions/%s/natives", p->client_dir, p->vanilla_version);

    static char assets_dir_path[MAX_PATH_SIZE];
    snprintf(assets_dir_path, sizeof(assets_dir_path),
             "%s/assets", p->client_dir);

    static char arg_libpath[MAX_PATH_SIZE + 32];
    snprintf(arg_libpath, sizeof(arg_libpath),
             "-Djava.library.path=%s", natives_dir);

    static char arg_launcher_brand[64];
    snprintf(arg_launcher_brand, sizeof(arg_launcher_brand),
             "-Dminecraft.launcher.brand=%s", CLIENT_BRAND);

    static char arg_client_brand[64];
    snprintf(arg_client_brand, sizeof(arg_client_brand),
             "-Dminecraft.client.brand=%s", CLIENT_BRAND);

    static char arg_bridge_port[48];
    snprintf(arg_bridge_port, sizeof(arg_bridge_port),
             "-Dceroclient.launcher.port=%d", p->bridge_port);

    static char asset_index_buf[64];
    snprintf(asset_index_buf, sizeof(asset_index_buf), "%s", p->asset_index);

    snprintf(bridge_port_buf, bridge_port_buf_sz, "%d", p->bridge_port);

    int n = 0;
    argv[n++] = p->java_exe;

    long xmx_mb = (p->ram_mb > 0) ? p->ram_mb : 2048;
    long xms_mb = xmx_mb / 2;
    if (xms_mb > 2048) xms_mb = 2048;
    if (xms_mb < 256) xms_mb = 256;

    static char arg_xms[32];
    static char arg_xmx[32];
    snprintf(arg_xms, sizeof(arg_xms), "-Xms%ldM", xms_mb);
    snprintf(arg_xmx, sizeof(arg_xmx), "-Xmx%ldM", xmx_mb);
    argv[n++] = arg_xms;
    argv[n++] = arg_xmx;

    argv[n++] = "-XX:+UnlockExperimentalVMOptions";
    argv[n++] = "-XX:+UseG1GC";
    argv[n++] = "-XX:+ParallelRefProcEnabled";
    argv[n++] = "-XX:MaxGCPauseMillis=200";
    argv[n++] = "-XX:+DisableExplicitGC";
    argv[n++] = "-XX:+AlwaysPreTouch";
    argv[n++] = "-XX:G1NewSizePercent=30";
    argv[n++] = "-XX:G1MaxNewSizePercent=40";
    argv[n++] = "-XX:G1HeapRegionSize=8M";
    argv[n++] = "-XX:G1ReservePercent=20";
    argv[n++] = "-XX:G1HeapWastePercent=5";
    argv[n++] = "-XX:G1MixedGCCountTarget=4";
    argv[n++] = "-XX:InitiatingHeapOccupancyPercent=15";
    argv[n++] = "-XX:G1MixedGCLiveThresholdPercent=90";
    argv[n++] = "-XX:G1RSetUpdatingPauseTimePercent=5";
    argv[n++] = "-XX:SurvivorRatio=32";
    argv[n++] = "-XX:+PerfDisableSharedMem";
    argv[n++] = "-XX:MaxTenuringThreshold=1";

    argv[n++] = "-Dfile.encoding=UTF-8";
    argv[n++] = "-Dstdout.encoding=UTF-8";
    argv[n++] = "-Dstderr.encoding=UTF-8";

    argv[n++] = arg_libpath;
    argv[n++] = arg_launcher_brand;
    argv[n++] = "-Dminecraft.launcher.version=1.0";
    argv[n++] = arg_client_brand;
    argv[n++] = arg_bridge_port;

#ifdef __APPLE__
    if (p->macos_first_thread) argv[n++] = "-XstartOnFirstThread";
#endif

    for (int i = 0; i < p->extra_jvm_count; i++) argv[n++] = p->extra_jvm_args[i];

    argv[n++] = "-cp";
    argv[n++] = p->classpath;

    if (p->has_cero) {
        argv[n++] = "fr.cerostudio.Main";
        log_msg("info", "Using CeroLoader as main class\n");
    } else {
        argv[n++] = p->game_main_class;
        log_msg("info", "Using vanilla main class: %s\n", p->game_main_class);
    }

    if (p->has_cero) {
        argv[n++] = "--realMainClass";
        argv[n++] = p->game_main_class;
    }

    argv[n++] = "--username";    argv[n++] = p->username;
    argv[n++] = "--version";     argv[n++] = p->version_id_for_args;
    argv[n++] = "--gameDir";     argv[n++] = p->client_dir;
    argv[n++] = "--assetsDir";   argv[n++] = assets_dir_path;
    argv[n++] = "--assetIndex";  argv[n++] = asset_index_buf;
    argv[n++] = "--uuid";        argv[n++] = p->uuid;
    argv[n++] = "--accessToken"; argv[n++] = p->token;
    argv[n++] = "--userProperties"; argv[n++] = "{}";
    argv[n++] = "--userType";    argv[n++] = "msa";
    argv[n++] = "--versionType"; argv[n++] = "release";
    argv[n++] = "--ceroMcVersion"; argv[n++] = p->vanilla_version;

    for (int i = 0; i < p->extra_game_count; i++) argv[n++] = p->extra_game_args[i];

    argv[n] = NULL;
    return n;
}