#ifndef CERO_LAUNCH_JVM_ARGS_H
#define CERO_LAUNCH_JVM_ARGS_H

#include <stddef.h>
#include "classpath.h"

#if defined(_WIN32)
  #define CP_SEP ";"
#else
  #define CP_SEP ":"
#endif

void build_classpath(char* out, size_t outsz,
                     int has_cero, const char* cero_jar_path,
                     const CpLib* libs, int libs_count,
                     const char* client_jar);

typedef struct {
    const char* java_exe;
    const char* classpath;
    const char* game_main_class;
    int         has_cero;
    const char* username;
    const char* version_id_for_args;
    const char* client_dir;
    const char* asset_index;
    const char* uuid;
    const char* token;
    const char* vanilla_version;
    int         bridge_port;
    long        ram_mb;

    int         macos_first_thread;

    const char* const* extra_jvm_args;
    int                extra_jvm_count;
    const char* const* extra_game_args;
    int                extra_game_count;
} LaunchParams;

int build_launch_argv(const LaunchParams* p, const char** argv, int max_argv,
                      char* bridge_port_buf, size_t bridge_port_buf_sz);

#endif 
