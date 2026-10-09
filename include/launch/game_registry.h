#ifndef CERO_GAME_REGISTRY_H
#define CERO_GAME_REGISTRY_H

#include <stddef.h>
#include "../utils/process.h"

unsigned game_registry_begin(const char* key);
void     game_registry_attach(const char* key, unsigned gen, process_handle_t h);
void     game_registry_finish(const char* key, unsigned gen);

int      game_registry_kill(const char* key);
int      game_registry_is_running(const char* key);
int      game_registry_running_count(void);

void     game_registry_running_json(char* out, size_t cap);

#endif