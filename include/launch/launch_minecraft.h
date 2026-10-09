#ifndef LAUNCH_MINECRAFT_H
#define LAUNCH_MINECRAFT_H

extern int local_bridge_port;

typedef void (*launch_progress_cb)(const char* step, int percent, void* userdata);

typedef struct {
    void*       ui;
    int*        game_running;
    const char* key;
    unsigned    gen;
} LaunchUserdata;

void launch_minecraft(const char* version,
                      launch_progress_cb cb, void* userdata);

int launch_instance(const char* instance_id,
                    launch_progress_cb cb, void* userdata);

#endif