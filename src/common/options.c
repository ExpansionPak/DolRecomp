#include "common/options.h"

#include <stdlib.h>

/* Fast is the default. It was measured at +6.7% fps on both Luigi's Mansion and
   Mario Kart independently (docs/AOT-PERFORMANCE-RESULTS.md 5q), and its two
   assumptions -- ram_size == GC_MAIN_RAM_SIZE, and no installed write journal
   -- are verified once at dispatch entry, so a runtime that breaks either gets
   the interpreter and a message rather than corrupted guest memory.

   The one runtime that does install a journal is ModernGekko's lockstep
   verifier, and only when STATICRECOMP_LOCKSTEP is set. That is the harness
   which compares the module against Dolphin's interpreter, so a module built
   for speed makes it inert: the guard refuses native execution and says so.
   Build with --memory-mode safe to run lockstep. */
int memory_mode_is_fast(void) {
    const char* value = getenv("DOLRECOMP_MEMORY_MODE");
    return !(value && value[0] == 's');
}

int replacements_enabled(void) {
    const char* value = getenv("DOLRECOMP_ENABLE_REPLACEMENTS");
    return value && value[0] && value[0] != '0';
}

int reg_args_enabled(void) {
    const char* value = getenv("DOLRECOMP_REG_ARGS");
    return value && value[0] == '1';
}
