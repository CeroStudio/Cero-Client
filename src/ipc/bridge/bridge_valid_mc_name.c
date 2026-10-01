#include "bridge_internal.h"
#include <string.h>
#include <ctype.h>

int bridge_valid_mc_name(const char* name) {
    if (!name) return 0;
    size_t l = strlen(name);
    if (l < 1 || l > 16) return 0;
    for (size_t i = 0; i < l; i++) {
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') return 0;
    }
    return 1;
}
