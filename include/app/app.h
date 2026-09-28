#ifndef CERO_APP_H
#define CERO_APP_H

typedef struct {
    const char* assets_path;
    const char* launch_version;
} LauncherOptions;

int launcher_parse_args(int argc, char** argv, LauncherOptions* opts);

int launcher_init(const LauncherOptions* opts);

int launcher_create_ui(void);

void launcher_bind_ui(void);

void launcher_start_services(void);

void launcher_shutdown(void);

#endif