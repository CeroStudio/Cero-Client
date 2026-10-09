#include "../../include/launch/game_registry.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
  #include <windows.h>
  static SRWLOCK g_reg_lock = SRWLOCK_INIT;
  #define REG_LOCK()   AcquireSRWLockExclusive(&g_reg_lock)
  #define REG_UNLOCK() ReleaseSRWLockExclusive(&g_reg_lock)
#else
  #include <pthread.h>
  static pthread_mutex_t g_reg_lock = PTHREAD_MUTEX_INITIALIZER;
  #define REG_LOCK()   pthread_mutex_lock(&g_reg_lock)
  #define REG_UNLOCK() pthread_mutex_unlock(&g_reg_lock)
#endif

#define MAX_GAMES 32

enum { SLOT_FREE = 0, SLOT_PREPARING = 1, SLOT_RUNNING = 2 };

typedef struct {
    int             state;
    unsigned        gen;
    char            key[64];
    process_handle_t proc;
} GameSlot;

static GameSlot g_slots[MAX_GAMES];
static unsigned g_next_gen = 1;

static GameSlot* find_slot(const char* key) {
    for (int i = 0; i < MAX_GAMES; i++)
        if (g_slots[i].state != SLOT_FREE && strcmp(g_slots[i].key, key) == 0)
            return &g_slots[i];
    return NULL;
}

unsigned game_registry_begin(const char* key) {
    if (!key || !key[0]) return 0;

    unsigned gen = 0;
    REG_LOCK();
    if (!find_slot(key)) {
        for (int i = 0; i < MAX_GAMES; i++) {
            if (g_slots[i].state == SLOT_FREE) {
                g_slots[i].state = SLOT_PREPARING;
                g_slots[i].gen   = gen = g_next_gen++;
                if (g_next_gen == 0) g_next_gen = 1;
                g_slots[i].proc  = PROCESS_HANDLE_INVALID;
                snprintf(g_slots[i].key, sizeof(g_slots[i].key), "%s", key);
                break;
            }
        }
    }
    REG_UNLOCK();
    return gen;
}

void game_registry_attach(const char* key, unsigned gen, process_handle_t h) {
    if (!key) return;
    REG_LOCK();
    GameSlot* s = find_slot(key);
    if (s && s->gen == gen) {
        s->proc  = h;
        s->state = SLOT_RUNNING;
    }
    REG_UNLOCK();
}

void game_registry_finish(const char* key, unsigned gen) {
    if (!key) return;
    REG_LOCK();
    GameSlot* s = find_slot(key);
    if (s && s->gen == gen) {
        s->state = SLOT_FREE;
        s->proc  = PROCESS_HANDLE_INVALID;
        s->key[0] = '\0';
    }
    REG_UNLOCK();
}

int game_registry_kill(const char* key) {
    if (!key) return 0;
    int hit = 0;
    REG_LOCK();
    GameSlot* s = find_slot(key);
    if (s && s->state == SLOT_RUNNING) {
        process_terminate(s->proc);
        hit = 1;
    }
    REG_UNLOCK();
    return hit;
}

int game_registry_is_running(const char* key) {
    int r = 0;
    REG_LOCK();
    GameSlot* s = key ? find_slot(key) : NULL;
    r = (s && s->state == SLOT_RUNNING);
    REG_UNLOCK();
    return r;
}

int game_registry_running_count(void) {
    int n = 0;
    REG_LOCK();
    for (int i = 0; i < MAX_GAMES; i++)
        if (g_slots[i].state == SLOT_RUNNING) n++;
    REG_UNLOCK();
    return n;
}

void game_registry_running_json(char* out, size_t cap) {
    if (!out || cap < 3) return;
    size_t len = 0;
    out[len++] = '[';

    REG_LOCK();
    int first = 1;
    for (int i = 0; i < MAX_GAMES; i++) {
        if (g_slots[i].state != SLOT_RUNNING) continue;
        const char* k = g_slots[i].key;
        int safe = 1;
        for (const char* p = k; *p; p++)
            if (*p == '"' || *p == '\\' || (unsigned char)*p < 0x20) { safe = 0; break; }
        if (!safe) continue;

        int n = snprintf(out + len, cap - len, "%s\"%s\"", first ? "" : ",", k);
        if (n < 0 || (size_t)n >= cap - len - 1) break;
        len += (size_t)n;
        first = 0;
    }
    REG_UNLOCK();

    out[len++] = ']';
    out[len] = '\0';
}