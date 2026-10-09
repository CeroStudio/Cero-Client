#include "../../include/launch/launch_lock.h"

#ifdef _WIN32
  #include <windows.h>
  static SRWLOCK g_prep_lock = SRWLOCK_INIT;
#else
  #include <pthread.h>
  static pthread_mutex_t g_prep_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

#ifdef _MSC_VER
  #define THREAD_LOCAL __declspec(thread)
#else
  #define THREAD_LOCAL __thread
#endif

static THREAD_LOCAL int t_held = 0;

void launch_prep_lock(launch_progress_cb cb, void* userdata) {
    if (t_held) return;

#ifdef _WIN32
    if (!TryAcquireSRWLockExclusive(&g_prep_lock)) {
        if (cb) cb("En attente d'un autre lancement...", 0, userdata);
        AcquireSRWLockExclusive(&g_prep_lock);
    }
#else
    if (pthread_mutex_trylock(&g_prep_lock) != 0) {
        if (cb) cb("En attente d'un autre lancement...", 0, userdata);
        pthread_mutex_lock(&g_prep_lock);
    }
#endif
    t_held = 1;
}

void launch_prep_unlock(void) {
    if (!t_held) return;
    t_held = 0;

#ifdef _WIN32
    ReleaseSRWLockExclusive(&g_prep_lock);
#else
    pthread_mutex_unlock(&g_prep_lock);
#endif
}
