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
    char output_copy[1200];
    char header_copy[1200];
    char object_copy[1200];
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
    snprintf(output_copy, sizeof(output_copy), "%s/out-copy", argv[2]);
    snprintf(header_copy, sizeof(header_copy),
             "%s/out-copy/generated/generated.h", argv[2]);
    snprintf(object_copy, sizeof(object_copy),
             "%s/out-copy/generated/chunks/chunk_0000_text0_80003100.o",
             argv[2]);
    CHECK(write_dol(dol));
    CHECK(make_dir(cache));
    CHECK(run_generator(argv[1], dol, single_output, "--targets=x86-64-v3",
                        cache));
    CHECK(run_native_generator(argv[1], dol, native_output, cache));
    CHECK(run_native_generator_batched(argv[1], dol, native_batch_output,
                                       cache, "3"));
    CHECK(run_generator(argv[1], dol, output, "--targets=x86-64-v2,x86-64-v3",
                        cache));
    CHECK(run_generator(argv[1], dol, output_copy,
                        "--targets=x86-64-v2,x86-64-v3", cache));
    CHECK(run_c_generator(argv[1], dol, c_output));
    FILE* file = fopen(c_smc, "rb");
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
    CHECK(strstr(text, "moderngekko_get_native_module") != NULL);
    CHECK(strstr(text, "moderngekko_native_region_available") != NULL);
    CHECK(strstr(text, "moderngekko_native_validate") != NULL);
    CHECK(strstr(text, "moderngekko_native_validate_all") != NULL);
    CHECK(strstr(text, "moderngekko_native_hash") != NULL);
    CHECK(strstr(text, "moderngekko_native_lookup") != NULL);
    CHECK(strstr(text, "moderngekko_native_needs_validation") != NULL);
    CHECK(strstr(text, "MODERNGEKKO_NATIVE_DIRTY, memory_order_relaxed") != NULL);
    CHECK(strstr(text, "\"TEST01\"") != NULL);
    CHECK(strstr(text, "CPUState") == NULL);
    CHECK(strstr(text, "moderngekko_commit_state") != NULL);
    CHECK(strstr(text, "moderngekko_reload_state") != NULL);
    file = fopen(native_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    CHECK(files_equal(native_header, native_batch_header));
    CHECK(files_equal(native_fallback, native_batch_fallback));
    u32 native_object_count = count_manifest_objects(native_manifest);
    u32 native_batch_object_count = count_manifest_objects(native_batch_manifest);
    CHECK(native_object_count != 0);
    CHECK(native_batch_object_count == (native_object_count + 2u) / 3u);
    CHECK(native_batch_object_count < native_object_count);
    file = fopen(native_batch_object, "rb");
    CHECK(file != NULL);
    CHECK(fread(magic, 1, 4, file) == 4);
    fclose(file);
    CHECK(is_native_object(magic));
    file = fopen(native_batch_bitcode, "rb");
    CHECK(file != NULL);
    fclose(file);
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
