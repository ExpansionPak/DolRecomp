#include "common/options.h"

#include <stdlib.h>

int memory_mode_is_fast(void) {
    const char* value = getenv("DOLRECOMP_MEMORY_MODE");
    return value && value[0] == 'f';
}
