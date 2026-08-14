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

/* Pass GPR3..GPR10 in registers across the private fastcc boundary, from
   DOLRECOMP_REG_ARGS. Caller and callee build the callee's signature
   independently, in different translation units and different object files, so
   this must have exactly one definition -- a disagreement is a wrong call
   across an object boundary with no diagnostic. */
int reg_args_enabled(void);

/* Leave guest state in CPUState instead of promoting it to allocas at region
   entry, from DOLRECOMP_STATE_MEMORY. Consulted from several translation units
   of the emitter, so it has one definition. */
int state_in_memory(void);

#ifdef __cplusplus
}
#endif

#endif
