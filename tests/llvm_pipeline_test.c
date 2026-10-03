#include "common/types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__,   \
                    #x);                                                       \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int make_dir(const char* path) {
#if defined(_WIN32)
    return _mkdir(path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0777) == 0 || errno == EEXIST;
#endif
}

// The emitted object format follows the default target triple, so this cannot
// assume ELF: a Windows host produces COFF, whose x86-64 objects start with the
// machine type IMAGE_FILE_MACHINE_AMD64 (0x8664) stored little-endian.
static int is_native_object(const u8* magic) {
#if defined(_WIN32)
    return magic[0] == 0x64 && magic[1] == 0x86;
#else
    return magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' &&
           magic[3] == 'F';
#endif
}

static int write_dol(const char* path) {
    u8 bytes[0x1100];
    memset(bytes, 0, sizeof(bytes));
    write_be32(bytes + 0x00, 0x100);
    write_be32(bytes + 0x48, 0x80003100u);
    write_be32(bytes + 0x90, 0x1000);
    write_be32(bytes + 0xE0, 0x80003100u);
    for (size_t offset = 0x100; offset < sizeof(bytes); offset += 4)
        write_be32(bytes + offset, 0x60000000u);
    write_be32(bytes + 0x100, 0x38600000u);
    write_be32(bytes + 0x104, 0x38630001u);
    write_be32(bytes + 0x108, 0x4200FFFCu);
    write_be32(bytes + 0x10C, 0x4E800020u);
    write_be32(bytes + 0x110, 0x480000F1u);
    write_be32(bytes + 0x114, 0x60000000u);
    write_be32(bytes + 0x118, 0x60000000u);
    write_be32(bytes + 0x11C, 0x60000000u);
    write_be32(bytes + 0x200, 0x4E800020u);
    FILE* file = fopen(path, "wb");
    if (!file)
        return 0;
    int ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    return fclose(file) == 0 && ok;
}

static int write_profile_hole_dol(const char* path) {
    u8 bytes[0x200];
    memset(bytes, 0, sizeof(bytes));
    write_be32(bytes + 0x00, 0x100);
    write_be32(bytes + 0x48, 0x80003100u);
    write_be32(bytes + 0x90, 0x100);
    write_be32(bytes + 0xE0, 0x80003100u);
    for (size_t offset = 0x100; offset < sizeof(bytes); offset += 4)
        write_be32(bytes + offset, 0x60000000u);
    write_be32(bytes + 0x100, 0x38600000u);
    write_be32(bytes + 0x104, 0x4E800020u);
    write_be32(bytes + 0x108, 0x00000000u);
    write_be32(bytes + 0x10C, 0x38600001u);
    write_be32(bytes + 0x110, 0x4E800020u);
    FILE* file = fopen(path, "wb");
    if (!file)
        return 0;
    int ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    return fclose(file) == 0 && ok;
}

static int write_successor_hole_dol(const char* path) {
    u8 bytes[0x400];
    memset(bytes, 0, sizeof(bytes));
    write_be32(bytes + 0x00, 0x100);
    write_be32(bytes + 0x48, 0x80003100u);
    write_be32(bytes + 0x90, 0x300);
    write_be32(bytes + 0xE0, 0x80003100u);
    for (size_t offset = 0x100; offset < sizeof(bytes); offset += 4)
        write_be32(bytes + offset, 0x60000000u);
    write_be32(bytes + 0x100, 0x48000200u);
    write_be32(bytes + 0x108, 0x480000F9u);
    write_be32(bytes + 0x10C, 0x480001F5u);
    write_be32(bytes + 0x200, 0x38600001u);
    write_be32(bytes + 0x204, 0x4E800020u);
    write_be32(bytes + 0x300, 0x38600002u);
    write_be32(bytes + 0x304, 0x4E800020u);
    FILE* file = fopen(path, "wb");
    if (!file)
        return 0;
    int ok = fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    return fclose(file) == 0 && ok;
}

static int run_generator(const char* executable, const char* dol,
                         const char* output, const char* targets,
                         const char* cache) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_CACHE", cache) != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", targets, "-j2", dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", cache, 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm", targets,
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator(const char* executable, const char* dol,
                                const char* output, const char* cache) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_CACHE", cache) != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "-j2", dol, output,
                   NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", cache, 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator_batched(const char* executable,
                                        const char* dol, const char* output,
                                        const char* cache,
                                        const char* ranges_per_object) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_CACHE", cache) != 0)
        return 0;
    if (_putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", ranges_per_object) != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "-j2", dol, output,
                   NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", cache, 1);
        setenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT", ranges_per_object, 1);
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator_fast(const char* executable, const char* dol,
                                     const char* output) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_CACHE", "off") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "3") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_FAST_ITERATION", "1") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RESUME", "1") != 0)
        return 0;
    int ok = _spawnl(_P_WAIT, executable, executable, "--gamecube",
                     "--backend=llvm", "--runtime=moderngekko",
                     "--game-id=TEST01", "--targets=host", "-j2", dol, output,
                     NULL) == 0;
    _putenv_s("DOLRECOMP_LLVM_FAST_ITERATION", "");
    _putenv_s("DOLRECOMP_LLVM_RESUME", "");
    return ok;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", "off", 1);
        setenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "3", 1);
        setenv("DOLRECOMP_LLVM_FAST_ITERATION", "1", 1);
        setenv("DOLRECOMP_LLVM_RESUME", "1", 1);
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator_hot(const char* executable, const char* dol,
                                    const char* output, const char* profile,
                                    const char* miss_min_samples) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_CACHE", "off") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "--range-profile",
                   profile, "--range-profile-coverage=100",
                   "--range-profile-miss-min-samples", miss_min_samples,
                   "--range-profile-neighbors=1", "-j2", dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", "off", 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "--range-profile", profile, "--range-profile-coverage=100",
              "--range-profile-miss-min-samples", miss_min_samples,
              "--range-profile-neighbors=1", "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator_forced_hot(const char* executable,
                                           const char* dol,
                                           const char* output,
                                           const char* profile,
                                           const char* entries) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_CACHE", "off") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "--range-profile",
                   profile, "--range-profile-coverage=100",
                   "--range-profile-miss-min-samples=101",
                   "--range-profile-neighbors=0",
                   "--range-profile-call-closure-depth=0",
                   "--range-profile-successor-closure-depth=0",
                   "--native-entry-points", entries,
                   "-j2", dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", "off", 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "--range-profile", profile, "--range-profile-coverage=100",
              "--range-profile-miss-min-samples=101",
              "--range-profile-neighbors=0",
              "--range-profile-call-closure-depth=0",
              "--range-profile-successor-closure-depth=0",
              "--native-entry-points", entries,
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_generator_patched(const char* executable, const char* dol,
                                 const char* output, const char* config) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_CACHE", "off") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "--native-abi=off",
                   "--config", config, "-j2", dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", "off", 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "--native-abi=off", "--config", config, "-j2", dol,
              output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_native_generator_successor_hot(const char* executable,
                                              const char* dol,
                                              const char* output,
                                              const char* profile,
                                              const char* successor_depth) {
#if defined(_WIN32)
    if (_putenv_s("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_CACHE", "off") != 0 ||
        _putenv_s("DOLRECOMP_LLVM_RANGES_PER_OBJECT", "") != 0)
        return 0;
    return _spawnl(_P_WAIT, executable, executable, "--gamecube",
                   "--backend=llvm", "--runtime=moderngekko",
                   "--game-id=TEST01", "--targets=host", "--range-profile",
                   profile, "--range-profile-coverage=100",
                   "--range-profile-neighbors=0",
                   "--range-profile-call-closure-depth=0",
                   "--range-profile-successor-closure-depth", successor_depth,
                   "-j2", dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        setenv("DOLRECOMP_LLVM_CHUNK_INSTRUCTIONS", "512", 1);
        setenv("DOLRECOMP_LLVM_CACHE", "off", 1);
        unsetenv("DOLRECOMP_LLVM_RANGES_PER_OBJECT");
        execl(executable, executable, "--gamecube", "--backend=llvm",
              "--runtime=moderngekko", "--game-id=TEST01", "--targets=host",
              "--range-profile", profile, "--range-profile-coverage=100",
              "--range-profile-neighbors=0",
              "--range-profile-call-closure-depth=0",
              "--range-profile-successor-closure-depth", successor_depth,
              "-j2", dol, output, NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int run_c_generator(const char* executable, const char* dol,
                           const char* output) {
#if defined(_WIN32)
    return _spawnl(_P_WAIT, executable, executable, "--gamecube", "--backend=c",
                   dol, output, NULL) == 0;
#else
    pid_t child = fork();
    if (child < 0)
        return 0;
    if (child == 0) {
        execl(executable, executable, "--gamecube", "--backend=c", dol, output,
              NULL);
        _exit(127);
    }
    int status = 0;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
#endif
}

static int files_equal(const char* first, const char* second) {
    FILE* a = fopen(first, "rb");
    FILE* b = fopen(second, "rb");
    if (!a || !b) {
        if (a)
            fclose(a);
        if (b)
            fclose(b);
        return 0;
    }
    int equal = 1;
    for (;;) {
        unsigned char left[4096];
        unsigned char right[4096];
        size_t left_count = fread(left, 1, sizeof(left), a);
        size_t right_count = fread(right, 1, sizeof(right), b);
        if (left_count != right_count || memcmp(left, right, left_count) != 0) {
            equal = 0;
            break;
        }
        if (left_count != sizeof(left))
            break;
    }
    fclose(a);
    fclose(b);
    return equal;
}

static u32 count_manifest_objects(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file)
        return 0;
    char line[2048];
    u32 count = 0;
    while (fgets(line, sizeof(line), file))
        count += strncmp(line, "// object: ", 11) == 0;
    fclose(file);
    return count;
}

int main(int argc, char** argv) {
    CHECK(argc == 3);
    CHECK(make_dir(argv[2]));
    char dol[1200];
    char output[1200];
    char header[1200];
    char object[1200];
    char call_target_object[1200];
    char second_object[1200];
    char v3_object[1200];
    char bitcode[1200];
    char manifest[1200];
    char cache[1200];
    char c_output[1200];
    char c_smc[1200];
    char single_output[1200];
    char native_output[1200];
    char native_header[1200];
    char native_object[1200];
    char native_fallback[1200];
    char native_manifest[1200];
    char native_batch_output[1200];
    char native_batch_header[1200];
    char native_batch_object[1200];
    char native_batch_bitcode[1200];
    char native_batch_fallback[1200];
    char native_batch_manifest[1200];
    char native_fast_output[1200];
    char native_fast_object[1200];
    char native_fast_bitcode[1200];
    char native_fast_manifest[1200];
    char native_hot_output[1200];
    char native_hot_header[1200];
    char native_hot_manifest[1200];
    char native_hot_profile[1200];
    char patch_output[1200];
    char patch_header[1200];
    char patch_config[1200];
    char profile_hole_dol[1200];
    char profile_hole_output[1200];
    char profile_hole_header[1200];
    char profile_hole_hot_output[1200];
    char profile_hole_hot_header[1200];
    char profile_hole_cold_output[1200];
    char profile_hole_cold_header[1200];
    char profile_hole_profile[1200];
    char profile_hole_forced_output[1200];
    char profile_hole_forced_header[1200];
    char forced_entries[1200];
    char successor_hole_dol[1200];
    char successor_profile[1200];
    char successor_cold_output[1200];
    char successor_cold_header[1200];
    char successor_hot_output[1200];
    char successor_hot_header[1200];
    char output_copy[1200];
    char header_copy[1200];
    char object_copy[1200];
    FILE* file = NULL;
    u8 magic[4];
    snprintf(dol, sizeof(dol), "%s/sample.dol", argv[2]);
    snprintf(output, sizeof(output), "%s/out", argv[2]);
    snprintf(header, sizeof(header), "%s/out/generated/generated.h", argv[2]);
    snprintf(object, sizeof(object),
             "%s/out/generated/chunks/chunk_0000_text0_80003100.o", argv[2]);
    snprintf(call_target_object, sizeof(call_target_object),
             "%s/out/generated/chunks/chunk_0002_text0_80003200.o", argv[2]);
    snprintf(second_object, sizeof(second_object),
             "%s/out/generated/chunks/chunk_0004_text0_80003A04.o", argv[2]);
    snprintf(v3_object, sizeof(v3_object),
             "%s/out/generated/chunks/chunk_0000_text0_80003100_x86_64_v3.o",
             argv[2]);
    snprintf(bitcode, sizeof(bitcode),
             "%s/out/generated/chunks/chunk_0000_text0_80003100.o.bc", argv[2]);
    snprintf(manifest, sizeof(manifest), "%s/out/generated/generated.c",
             argv[2]);
    snprintf(cache, sizeof(cache), "%s/cache", argv[2]);
    snprintf(c_output, sizeof(c_output), "%s/out-c", argv[2]);
    snprintf(c_smc, sizeof(c_smc), "%s/out-c/generated/generated_smc.txt",
             argv[2]);
    snprintf(single_output, sizeof(single_output), "%s/out-single", argv[2]);
    snprintf(native_output, sizeof(native_output), "%s/out-native", argv[2]);
    snprintf(native_header, sizeof(native_header),
             "%s/out-native/generated/generated.h", argv[2]);
    snprintf(native_object, sizeof(native_object),
             "%s/out-native/generated/chunks/chunk_0000_text0_80003100.o",
             argv[2]);
    snprintf(native_fallback, sizeof(native_fallback),
             "%s/out-native/generated/generated_fallbacks.csv", argv[2]);
    snprintf(native_manifest, sizeof(native_manifest),
             "%s/out-native/generated/generated.c", argv[2]);
    snprintf(native_batch_output, sizeof(native_batch_output),
             "%s/out-native-batch", argv[2]);
    snprintf(native_batch_header, sizeof(native_batch_header),
             "%s/out-native-batch/generated/generated.h", argv[2]);
    snprintf(native_batch_object, sizeof(native_batch_object),
             "%s/out-native-batch/generated/chunks/"
             "chunk_0000_text0_80003100_r3.o", argv[2]);
    snprintf(native_batch_bitcode, sizeof(native_batch_bitcode),
             "%s/out-native-batch/generated/chunks/"
             "chunk_0000_text0_80003100_r3.o.bc", argv[2]);
    snprintf(native_batch_fallback, sizeof(native_batch_fallback),
             "%s/out-native-batch/generated/generated_fallbacks.csv",
             argv[2]);
    snprintf(native_batch_manifest, sizeof(native_batch_manifest),
             "%s/out-native-batch/generated/generated.c", argv[2]);
    snprintf(native_fast_output, sizeof(native_fast_output), "%s/out-native-fast",
             argv[2]);
    snprintf(native_fast_object, sizeof(native_fast_object),
             "%s/out-native-fast/generated/chunks/"
             "chunk_0000_text0_80003100_r3.o", argv[2]);
    snprintf(native_fast_bitcode, sizeof(native_fast_bitcode),
             "%s/out-native-fast/generated/chunks/"
             "chunk_0000_text0_80003100_r3.o.bc", argv[2]);
    snprintf(native_fast_manifest, sizeof(native_fast_manifest),
             "%s/out-native-fast/generated/generated.c", argv[2]);
    snprintf(native_hot_output, sizeof(native_hot_output), "%s/out-native-hot",
             argv[2]);
    snprintf(native_hot_header, sizeof(native_hot_header),
             "%s/out-native-hot/generated/generated.h", argv[2]);
    snprintf(native_hot_manifest, sizeof(native_hot_manifest),
             "%s/out-native-hot/generated/generated.c", argv[2]);
    snprintf(native_hot_profile, sizeof(native_hot_profile), "%s/hot.csv",
             argv[2]);
    snprintf(patch_output, sizeof(patch_output),
             "%s/out-patch", argv[2]);
    snprintf(patch_header, sizeof(patch_header),
             "%s/out-patch/generated/generated.h", argv[2]);
    snprintf(patch_config, sizeof(patch_config),
             "%s/config.toml", argv[2]);
    snprintf(profile_hole_dol, sizeof(profile_hole_dol), "%s/profile-hole.dol",
             argv[2]);
    snprintf(profile_hole_output, sizeof(profile_hole_output),
             "%s/out-profile-hole", argv[2]);
    snprintf(profile_hole_header, sizeof(profile_hole_header),
             "%s/out-profile-hole/generated/generated.h", argv[2]);
    snprintf(profile_hole_hot_output, sizeof(profile_hole_hot_output),
             "%s/out-profile-hole-hot", argv[2]);
    snprintf(profile_hole_hot_header, sizeof(profile_hole_hot_header),
             "%s/out-profile-hole-hot/generated/generated.h", argv[2]);
    snprintf(profile_hole_cold_output, sizeof(profile_hole_cold_output),
             "%s/out-profile-hole-cold", argv[2]);
    snprintf(profile_hole_cold_header, sizeof(profile_hole_cold_header),
             "%s/out-profile-hole-cold/generated/generated.h", argv[2]);
    snprintf(profile_hole_profile, sizeof(profile_hole_profile),
             "%s/profile-hole.csv", argv[2]);
    snprintf(profile_hole_forced_output, sizeof(profile_hole_forced_output),
             "%s/out-profile-hole-forced", argv[2]);
    snprintf(profile_hole_forced_header, sizeof(profile_hole_forced_header),
             "%s/out-profile-hole-forced/generated/generated.h", argv[2]);
    snprintf(forced_entries, sizeof(forced_entries),
             "%s/native-entry-points.txt", argv[2]);
    snprintf(successor_hole_dol, sizeof(successor_hole_dol),
             "%s/successor-hole.dol", argv[2]);
    snprintf(successor_profile, sizeof(successor_profile),
             "%s/successor-hot.csv", argv[2]);
    snprintf(successor_cold_output, sizeof(successor_cold_output),
             "%s/out-successor-cold", argv[2]);
    snprintf(successor_cold_header, sizeof(successor_cold_header),
             "%s/out-successor-cold/generated/generated.h", argv[2]);
    snprintf(successor_hot_output, sizeof(successor_hot_output),
             "%s/out-successor-hot", argv[2]);
    snprintf(successor_hot_header, sizeof(successor_hot_header),
             "%s/out-successor-hot/generated/generated.h", argv[2]);
    snprintf(output_copy, sizeof(output_copy), "%s/out-copy", argv[2]);
    snprintf(header_copy, sizeof(header_copy),
             "%s/out-copy/generated/generated.h", argv[2]);
    snprintf(object_copy, sizeof(object_copy),
             "%s/out-copy/generated/chunks/chunk_0000_text0_80003100.o",
             argv[2]);
    CHECK(write_dol(dol));
    CHECK(write_profile_hole_dol(profile_hole_dol));
    CHECK(write_successor_hole_dol(successor_hole_dol));
    CHECK(make_dir(cache));
    file = fopen(native_hot_profile, "wb");
    CHECK(file != NULL);
    CHECK(fputs("kind,pc,samples\nnative_cycles,80003100,100\n", file) >= 0);
    CHECK(fclose(file) == 0);
    file = fopen(successor_profile, "wb");
    CHECK(file != NULL);
    CHECK(fputs("kind,pc,samples\n"
                "native_cycles,80003100,100\n",
                file) >= 0);
    CHECK(fclose(file) == 0);
    file = fopen(profile_hole_profile, "wb");
    CHECK(file != NULL);
    CHECK(fputs("kind,pc,samples\n"
                "native_cycles,80003100,100\n"
                "module_miss,8000310c,100\n",
                file) >= 0);
    CHECK(fclose(file) == 0);
    file = fopen(forced_entries, "wb");
    CHECK(file != NULL);
    CHECK(fputs("# explicit mod hook entry\n0x8000310c\n", file) >= 0);
    CHECK(fclose(file) == 0);
    file = fopen(patch_config, "wb");
    CHECK(file != NULL);
    CHECK(fputs("[patches]\n"
                "abi_version = 1\n\n"
                "[[patches.func]]\n"
                "start = 0x80003110\n"
                "end = 0x80003204\n"
                "symbol = \"test_patch\"\n"
                "source = \"dummy.c\"\n"
                "expected_fnv64 = \"3D6E239CE93042B4\"\n",
                file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(run_generator(argv[1], dol, single_output, "--targets=x86-64-v3",
                        cache));
    CHECK(run_native_generator(argv[1], dol, native_output, cache));
    CHECK(run_native_generator_batched(argv[1], dol, native_batch_output,
                                       cache, "3"));
    CHECK(run_native_generator_fast(argv[1], dol, native_fast_output));
    CHECK(run_native_generator_fast(argv[1], dol, native_fast_output));
    CHECK(run_native_generator_hot(argv[1], dol, native_hot_output,
                                   native_hot_profile, "1"));
    CHECK(run_generator_patched(argv[1], dol, patch_output, patch_config));
    CHECK(run_native_generator(argv[1], profile_hole_dol, profile_hole_output,
                               cache));
    CHECK(run_native_generator_hot(argv[1], profile_hole_dol,
                                   profile_hole_hot_output,
                                   profile_hole_profile, "1"));
    CHECK(run_native_generator_hot(argv[1], profile_hole_dol,
                                   profile_hole_cold_output,
                                   profile_hole_profile, "101"));
    CHECK(run_native_generator_forced_hot(argv[1], profile_hole_dol,
                                          profile_hole_forced_output,
                                          profile_hole_profile,
                                          forced_entries));
    CHECK(run_native_generator_successor_hot(argv[1], successor_hole_dol,
                                             successor_cold_output,
                                             successor_profile, "0"));
    CHECK(run_native_generator_successor_hot(argv[1], successor_hole_dol,
                                             successor_hot_output,
                                             successor_profile, "1"));
    CHECK(run_generator(argv[1], dol, output, "--targets=x86-64-v2,x86-64-v3",
                        cache));
    CHECK(run_generator(argv[1], dol, output_copy,
                        "--targets=x86-64-v2,x86-64-v3", cache));
    CHECK(run_c_generator(argv[1], dol, c_output));
    file = fopen(c_smc, "rb");
    CHECK(file != NULL);
    fclose(file);
    file = fopen(header, "rb");
    CHECK(file != NULL);
    char text[65536];
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "DOLRECOMP_BACKEND_LLVM") != NULL);
    CHECK(strstr(text, "DOLRECOMP_MODULE_ABI_V4") != NULL);
    CHECK(strstr(text, "x86-64-v3-exact") != NULL);
    CHECK(strstr(text, "backend/module_abi.h") == NULL);
    file = fopen(native_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "Core/PowerPC/Native/NativeModuleABI.h") != NULL);
    CHECK(strstr(text, "#include <stddef.h>") != NULL);
    CHECK(strstr(text, "moderngekko_get_native_module") != NULL);
    CHECK(strstr(text, "moderngekko_native_region_available") != NULL);
    CHECK(strstr(text, "moderngekko_native_validate") != NULL);
    CHECK(strstr(text, "moderngekko_native_validate_all") != NULL);
    CHECK(strstr(text, "moderngekko_native_hash") != NULL);
    CHECK(strstr(text, "moderngekko_native_lookup") != NULL);
    CHECK(strstr(text, "moderngekko_native_supports_entry") != NULL);
    CHECK(strstr(text, "moderngekko_native_needs_validation") != NULL);
    CHECK(strstr(text, "MODERNGEKKO_NATIVE_DIRTY, memory_order_relaxed") != NULL);
    CHECK(strstr(text, "\"TEST01\"") != NULL);
    CHECK(strstr(text, "CPUState") == NULL);
    CHECK(strstr(text, "moderngekko_commit_state") != NULL);
    CHECK(strstr(text, "moderngekko_reload_state") != NULL);
    CHECK(strstr(text, "offsetof(MGNativeState, try_write_registers)") != NULL);
    CHECK(strstr(text, "offsetof(MGNativeState, try_read_registers)") != NULL);
    CHECK(strstr(text, "state->try_write_registers") != NULL);
    CHECK(strstr(text, "state->try_read_registers") != NULL);
    CHECK(strstr(text, "values + MG_STATE_GPR0, values + MG_STATE_FPR0") != NULL);
    CHECK(strstr(text,
                 "values + MG_STATE_PS1_0, gpr_mask, ps0_mask, ps1_mask") != NULL);
    CHECK(strstr(text, "moderngekko_native_entry_offsets") != NULL);
    CHECK(strstr(text, "services->begin_native_segment") == NULL);
    CHECK(strstr(text,
                 "uint64_t state_values[MODERNGEKKO_NATIVE_STATE_COUNT] = {0};") !=
          NULL);
    CHECK(strstr(text,
                 "uint64_t dirty_mask[MODERNGEKKO_NATIVE_STATE_MASK_WORDS] = {0};") !=
          NULL);
    CHECK(strstr(text,
                 "uint64_t valid_mask[MODERNGEKKO_NATIVE_STATE_MASK_WORDS] = {0};") !=
          NULL);
    CHECK(strstr(text,
                 "runtime, state, state_values, dirty_mask, valid_mask") != NULL);
    CHECK(strstr(text,
                 "moderngekko_commit_state(state, state_values, dirty_mask)") !=
          NULL);
    CHECK(strstr(text, "local_cycles < remaining") != NULL);
    CHECK(strstr(text, "0u, 4u, 64u, 65u, 577u") != NULL);
    CHECK(strstr(text, "UINT64_C(0x000000000000003B)") != NULL);
    CHECK(strstr(text, "if ((address - range->start) & 3u) return 0;") != NULL);
    CHECK(strstr(text,
                 "moderngekko_native_reset, moderngekko_native_supports_entry") !=
          NULL);
    file = fopen(native_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    CHECK(files_equal(native_header, native_batch_header));
    CHECK(files_equal(native_fallback, native_batch_fallback));
    u32 native_object_count = count_manifest_objects(native_manifest);
    u32 native_batch_object_count = count_manifest_objects(native_batch_manifest);
    u32 native_hot_object_count = count_manifest_objects(native_hot_manifest);
    CHECK(native_object_count != 0);
    CHECK(native_batch_object_count == (native_object_count + 2u) / 3u);
    CHECK(native_batch_object_count < native_object_count);
    CHECK(native_hot_object_count >= 3u);
    CHECK(native_hot_object_count < native_object_count);
    file = fopen(native_hot_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "func_80003100") != NULL);
    CHECK(strstr(text, "func_80003200") != NULL);
    CHECK(strstr(text, "func_80003A04") == NULL);
    file = fopen(patch_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "extern MGNativeExit test_patch") != NULL);
    CHECK(strstr(text, "MGNativeExit func_80003110(") != NULL);
    CHECK(strstr(text, "return test_patch(runtime, state, entry_pc, "
                       "cycle_budget, cycle_base);") != NULL);
    CHECK(strstr(text, "{0x80003110u, 0x80003204u,") != NULL);
    CHECK(strstr(text, "func_80003200") == NULL);
    file = fopen(profile_hole_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "{0x8000310Cu, 0x80003114u,") == NULL);
    file = fopen(profile_hole_forced_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "{0x8000310Cu, 0x80003114u,") != NULL);
    file = fopen(successor_cold_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "func_80003200") == NULL);
    CHECK(strstr(text, "func_80003300") == NULL);
    file = fopen(successor_hot_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "func_80003200") != NULL);
    CHECK(strstr(text, "func_80003300") != NULL);
    file = fopen(profile_hole_hot_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "{0x8000310Cu, 0x80003114u,") != NULL);
    file = fopen(profile_hole_cold_header, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "{0x8000310Cu, 0x80003114u,") == NULL);
    file = fopen(native_batch_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(native_batch_bitcode, "rb");
    CHECK(file != NULL);
    fclose(file);
    file = fopen(native_fast_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(native_fast_bitcode, "rb");
    CHECK(file == NULL);
    file = fopen(native_fast_manifest, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "ThinLTO summaries") == NULL);
    file = fopen(manifest, "rb");
    CHECK(file != NULL);
    length = fread(text, 1, sizeof(text) - 1, file);
    text[length] = '\0';
    fclose(file);
    CHECK(strstr(text, "// object: chunks/") != NULL);
    file = fopen(object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(bitcode, "rb");
    CHECK(file != NULL);
    fclose(file);
    CHECK(files_equal(header, header_copy));
    CHECK(files_equal(object, object_copy));
    file = fopen(v3_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(call_target_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(second_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    return 0;
}
