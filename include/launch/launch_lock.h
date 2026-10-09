#ifndef CERO_LAUNCH_LOCK_H
#define CERO_LAUNCH_LOCK_H

#include "launch_minecraft.h"

void launch_prep_lock(launch_progress_cb cb, void* userdata);

void launch_prep_unlock(void);

#endif