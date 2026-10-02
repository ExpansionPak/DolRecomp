#include "app/cli.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s:%d: %s\n", \
    __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    char* valid[] = {
        "dolrecomp", "--gamecube", "--backend=llvm",
        "--targets=x86-64-v2,x86-64-v3", "--semantics=fast",
        "--state-in-memory",
        "--instrumentation=lockstep", "--profile-use=profile.profdata",
        "--native-abi=compact",
        "--runtime=moderngekko", "--game-id", "GMSE8P",
        "--range-profile=runtime.csv", "--range-profile-coverage=98.5",
        "--range-profile-min-samples=7", "--range-profile-miss-min-samples=11",
        "--range-profile-neighbors=2",
        "--range-profile-call-closure-depth=3",
        "--partition-instructions", "512", "--partition-seed", "42",
        "input.dol", "output",
    };
    CliOptions options;
#ifdef DOLRECOMP_ENABLE_LLVM
    CHECK(parse_cli((int)(sizeof(valid) / sizeof(valid[0])), valid, &options));
    CHECK(options.backend == DOLRECOMP_BACKEND_LLVM);
    CHECK(strcmp(options.llvm_targets, "x86-64-v2,x86-64-v3") == 0);
    CHECK(options.fast_semantics && options.lockstep_instrumentation);
    CHECK(options.state_in_memory);
    CHECK(options.llvm_native_abi == 1u);
    CHECK(options.llvm_runtime == 1u);
    CHECK(strcmp(options.game_id, "GMSE8P") == 0);
    CHECK(strcmp(options.profile_use_path, "profile.profdata") == 0);
    CHECK(strcmp(options.range_profile_path, "runtime.csv") == 0);
    CHECK(options.range_profile_coverage == 98.5);
    CHECK(options.range_profile_min_samples == 7u);
    CHECK(options.range_profile_miss_min_samples == 11u);
    CHECK(options.range_profile_neighbors == 2u);
    CHECK(options.range_profile_call_closure_depth == 3u);
    CHECK(options.partition_instructions == 512u);
    CHECK(options.partition_seed == 42u);

    char* inherited_miss_min[] = {
        "dolrecomp", "--gamecube", "--backend=llvm",
        "--runtime=moderngekko", "--range-profile=runtime.csv",
        "--range-profile-min-samples=23", "input.dol", "output",
    };
    CHECK(parse_cli((int)(sizeof(inherited_miss_min) /
                          sizeof(inherited_miss_min[0])),
                    inherited_miss_min, &options));
    CHECK(options.range_profile_min_samples == 23u);
    CHECK(options.range_profile_miss_min_samples == 23u);

    char* explicit_miss_min[] = {
        "dolrecomp", "--gamecube", "--backend=llvm",
        "--runtime=moderngekko", "--range-profile=runtime.csv",
        "--range-profile-miss-min-samples=31",
        "--range-profile-min-samples=23", "input.dol", "output",
    };
    CHECK(parse_cli((int)(sizeof(explicit_miss_min) /
                          sizeof(explicit_miss_min[0])),
                    explicit_miss_min, &options));
    CHECK(options.range_profile_min_samples == 23u);
    CHECK(options.range_profile_miss_min_samples == 31u);
#else
    (void)valid;
#endif

    char* bad_target[] = {
        "dolrecomp", "--gamecube", "--targets=x86-64-v4", "input.dol",
    };
    CHECK(!parse_cli((int)(sizeof(bad_target) / sizeof(bad_target[0])),
                     bad_target, &options));

    char* conflicting_profile[] = {
        "dolrecomp", "--gamecube", "--profile-use=one",
        "--profile-generate=two", "input.dol",
    };
    CHECK(!parse_cli(
        (int)(sizeof(conflicting_profile) / sizeof(conflicting_profile[0])),
        conflicting_profile, &options));

#ifdef DOLRECOMP_ENABLE_LLVM
    char* small_chunks[] = {
        "dolrecomp", "--gamecube", "--backend=llvm",
        "--partition-instructions", "64", "input.dol", "output",
    };
    CHECK(parse_cli((int)(sizeof(small_chunks) / sizeof(small_chunks[0])),
                    small_chunks, &options));
    CHECK(options.partition_instructions == 64u);
#endif
    return 0;
}
