#include "bridge_internal.h"
#include "../../../include/ipc/bridge.h"

#ifdef _WIN32
void bridge_start(void) {
    bridge_lock_init();
    CreateThread(NULL, 0, bridge_thread, NULL, 0, NULL);
    Sleep(100);
}
#else
void bridge_start(void) {
    pthread_t bridge_tid;
    pthread_create(&bridge_tid, NULL, bridge_thread, NULL);
    pthread_detach(bridge_tid);
    usleep(100000);
}
#endif
