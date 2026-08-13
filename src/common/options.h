#ifndef DOLRECOMP_COMMON_OPTIONS_H
#define DOLRECOMP_COMMON_OPTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Guest memory lowering mode, from DOLRECOMP_MEMORY_MODE (set by
   --memory-mode). Lives here rather than in either backend because the C
   dispatch emitter and the LLVM memory lowering both consult it, and a
   disagreement between them would emit a fast-path body behind a guard that
   does not check its assumptions -- or the reverse. One definition, one
   answer. */
int memory_mode_is_fast(void);

/* Runtime function replacement, from DOLRECOMP_ENABLE_REPLACEMENTS. When on,
   external transfers must leave through the dispatcher so a replacement gets
   its chance; a direct call would bypass it silently. */
int replacements_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
