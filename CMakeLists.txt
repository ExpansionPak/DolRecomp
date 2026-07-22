cmake_minimum_required(VERSION 3.16)
project(DolRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Portable warnings for local development and CI.
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

if(NOT DEFINED ZLIB_ROOT)
    if(DEFINED ENV{DEVKITPRO})
        set(_devkitpro "$ENV{DEVKITPRO}")
        if(MSYS AND _devkitpro MATCHES "^[A-Za-z]:")
            execute_process(
                COMMAND cygpath -u "${_devkitpro}"
                OUTPUT_VARIABLE _devkitpro
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
        endif()
    elseif(EXISTS "/opt/devkitpro")
        set(_devkitpro "/opt/devkitpro")
    endif()

    if(DEFINED _devkitpro)
        set(_devkitpro_zlib "${_devkitpro}/msys2/ucrt64")
        if(EXISTS "${_devkitpro_zlib}/include/zlib.h")
            set(ZLIB_ROOT "${_devkitpro_zlib}")
        endif()
    endif()
endif()

find_package(ZLIB QUIET)
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

set(DOLRECOMP_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(dr_cpu STATIC src/cpu/cpu.c)
target_include_directories(dr_cpu PUBLIC ${DOLRECOMP_SRC})
if(NOT WIN32)
    target_link_libraries(dr_cpu PUBLIC m)
endif()

add_library(dr_platform STATIC
    src/platform/fs.c
    src/platform/strutil.c
    src/platform/pathlist.c
    src/platform/process.c
)
target_include_directories(dr_platform PUBLIC ${DOLRECOMP_SRC})

add_library(dr_frontend STATIC
    src/frontend/decoder.c
    src/frontend/container/dol.c
    src/frontend/container/rel.c
    src/frontend/container/rpx.c
    src/frontend/container/disc_extract.c
)
target_include_directories(dr_frontend PUBLIC ${DOLRECOMP_SRC})
if(ZLIB_FOUND)
    target_compile_definitions(dr_frontend PUBLIC DOLRECOMP_HAVE_ZLIB)
    target_link_libraries(dr_frontend PUBLIC ZLIB::ZLIB)
endif()

add_library(dr_analysis STATIC
    src/analysis/embedded_data.c
    src/analysis/smc.c
    src/analysis/symbol_map.c
)
target_include_directories(dr_analysis PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_analysis PUBLIC dr_frontend)

add_library(dr_backend STATIC
    src/backend/emitter.c
    src/backend/dispatch.c
    src/backend/codegen.c
    src/backend/symbols.c
)
target_include_directories(dr_backend PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_backend PUBLIC dr_frontend dr_analysis)
if(NOT WIN32)
    target_link_libraries(dr_backend PUBLIC Threads::Threads)
endif()

add_library(dr_app STATIC
    src/app/cli.c
    src/app/database.c
    src/app/setup.c
    src/app/paths.c
    src/app/pipeline.c
)
target_include_directories(dr_app PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_app PUBLIC dr_backend dr_analysis dr_frontend dr_platform)

add_executable(dolrecomp src/app/main.c)
target_link_libraries(dolrecomp PRIVATE dr_app)

enable_testing()

add_executable(test_opcodes tests/test_opcodes.c)
target_link_libraries(test_opcodes PRIVATE dr_frontend)
add_test(NAME opcodes COMMAND test_opcodes)

add_executable(test_cross_check tests/test_cross_check.c)
target_link_libraries(test_cross_check PRIVATE dr_frontend)
add_test(NAME cross_check COMMAND test_cross_check)

add_executable(test_dol tests/test_dol.c)
target_link_libraries(test_dol PRIVATE dr_frontend)
add_test(NAME dol COMMAND test_dol)

add_executable(test_rel tests/test_rel.c)
target_link_libraries(test_rel PRIVATE dr_frontend)
add_test(NAME rel COMMAND test_rel)

add_executable(test_rpx tests/test_rpx.c)
target_link_libraries(test_rpx PRIVATE dr_frontend)
add_test(NAME rpx COMMAND test_rpx)

add_executable(test_disc_extract tests/test_disc_extract.c)
target_include_directories(test_disc_extract PRIVATE ${DOLRECOMP_SRC})
add_test(NAME disc_extract
    COMMAND test_disc_extract
        $<SHELL_PATH:$<TARGET_FILE:dolrecomp>>
        $<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/disc_extract_test>
)

add_executable(test_pc_reference tests/test_pc_reference.c)
target_link_libraries(test_pc_reference PRIVATE dr_frontend dr_cpu)
add_test(NAME pc_reference COMMAND test_pc_reference)

add_executable(test_jumptables tests/test_jumptables.c)
target_link_libraries(test_jumptables PRIVATE dr_backend dr_cpu)
add_test(NAME jumptables COMMAND test_jumptables)

add_executable(test_dispatch tests/test_dispatch.c)
target_link_libraries(test_dispatch PRIVATE dr_backend)
add_test(NAME dispatch COMMAND test_dispatch)

add_executable(test_symbols tests/test_symbols.c)
target_link_libraries(test_symbols PRIVATE dr_backend)
add_test(NAME symbols COMMAND test_symbols
    $<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/codewarrior.map>)

add_executable(test_codegen_emit tests/test_codegen_emit.c)
target_link_libraries(test_codegen_emit PRIVATE dr_backend)
add_test(NAME codegen_compile
    COMMAND ${CMAKE_COMMAND}
        -DGENERATOR_EXE=$<TARGET_FILE:test_codegen_emit>
        -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_check.c
        -DREPO_SRC=${CMAKE_CURRENT_SOURCE_DIR}/src
        -DHOST_GENERATOR=${CMAKE_GENERATOR}
        -DHOST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
        -DHOST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
        -DHOST_C_COMPILER=${CMAKE_C_COMPILER}
        -DHOST_BUILD_CONFIG=$<CONFIG>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/codegen_compile.cmake
)

# Emitted-code acceptance differential.
# Decode + emit each probe through the REAL emitter, run the generated C, and
# byte-compare the resulting CPU state against real-Gekko expectations (oracle).
# Catches emitter-semantics defects -- known (catalogued xfail) and unknown.
add_executable(diff_gen
    tests/diff/gen.c
    src/frontend/decoder.c
    src/backend/emitter.c
)
target_include_directories(diff_gen PRIVATE src tests/diff)

set(DIFF_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/probes_generated.c)
add_custom_command(
    OUTPUT ${DIFF_GENERATED}
    COMMAND $<TARGET_FILE:diff_gen> ${DIFF_GENERATED}
    DEPENDS diff_gen
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/diff/probes.inc
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/diff/probe.h
    COMMENT "Emitting instruction probes through the DolRecomp emitter"
)

add_executable(test_emitted_diff
    tests/diff/run.c
    ${DIFF_GENERATED}
    src/cpu/cpu.c
)
target_include_directories(test_emitted_diff PRIVATE src tests/diff)
# Match Gekko IEEE-754 round-once semantics on the host: no fma contraction,
# no fast-math reassociation.
if(NOT MSVC)
    target_compile_options(test_emitted_diff PRIVATE -ffp-contract=off -fno-fast-math)
endif()
if(NOT WIN32)
    target_link_libraries(test_emitted_diff PRIVATE m)
endif()
add_test(NAME emitted_diff COMMAND test_emitted_diff)

# Dedicated emitted-code checks for non-fallthrough/control/system/cache forms
# that cannot live in the ordinary producer-DOL fallthrough matrix.
add_executable(dedicated_diff_gen
    tests/diff/dedicated_gen.c
    src/frontend/decoder.c
    src/backend/emitter.c
)
target_include_directories(dedicated_diff_gen PRIVATE src tests/diff)

set(DEDICATED_GENERATED ${CMAKE_CURRENT_BINARY_DIR}/dedicated_generated.c)
add_custom_command(
    OUTPUT ${DEDICATED_GENERATED}
    COMMAND $<TARGET_FILE:dedicated_diff_gen> ${DEDICATED_GENERATED}
    DEPENDS dedicated_diff_gen
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/diff/dedicated_cases.h
    COMMENT "Emitting dedicated control/system probes through the DolRecomp emitter"
)

add_executable(test_dedicated_diff
    tests/diff/dedicated_run.c
    ${DEDICATED_GENERATED}
    src/cpu/cpu.c
)
target_include_directories(test_dedicated_diff PRIVATE src tests/diff)
if(NOT MSVC)
    target_compile_options(test_dedicated_diff PRIVATE -ffp-contract=off -fno-fast-math)
endif()
if(NOT WIN32)
    target_link_libraries(test_dedicated_diff PRIVATE m)
endif()
add_test(NAME dedicated_diff COMMAND test_dedicated_diff)
