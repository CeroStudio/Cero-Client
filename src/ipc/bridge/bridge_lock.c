#include "bridge_internal.h"

#ifdef _WIN32
static CRITICAL_SECTION g_game_lock;
static int g_game_lock_init = 0;
#else
static pthread_mutex_t g_game_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

void bridge_lock_init(void) {
#ifdef _WIN32
    if (!g_game_lock_init) {
        InitializeCriticalSection(&g_game_lock);
        g_game_lock_init = 1;
    }
#endif
}

void bridge_game_lock(void) {
#ifdef _WIN32
    if (g_game_lock_init) EnterCriticalSection(&g_game_lock);
#else
    pthread_mutex_lock(&g_game_lock);
#endif
}

void bridge_game_unlock(void) {
#ifdef _WIN32
    if (g_game_lock_init) LeaveCriticalSection(&g_game_lock);
#else
    pthread_mutex_unlock(&g_game_lock);
#endif
}
