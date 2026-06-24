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

add_executable(dolrecomp
    src/main.c
    src/frontend/dol.c
    src/frontend/rel.c
    src/frontend/rpx.c
    src/frontend/disc_extract.c
    src/frontend/decoder.c
    src/backend/emitter.c
)

target_include_directories(dolrecomp PRIVATE src)
if(ZLIB_FOUND)
    target_compile_definitions(dolrecomp PRIVATE DOLRECOMP_HAVE_ZLIB)
    target_link_libraries(dolrecomp PRIVATE ZLIB::ZLIB)
endif()
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(dolrecomp PRIVATE Threads::Threads)
endif()

# opcode verification tests
add_executable(test_opcodes
    tests/test_opcodes.c
    src/frontend/decoder.c
)
target_include_directories(test_opcodes PRIVATE src)

enable_testing()
add_test(NAME opcodes COMMAND test_opcodes)

# cross-check against devkitPPC machine code
add_executable(test_cross_check
    tests/test_cross_check.c
    src/frontend/decoder.c
)
target_include_directories(test_cross_check PRIVATE src)
add_test(NAME cross_check COMMAND test_cross_check)

add_executable(test_dol
    tests/test_dol.c
    src/frontend/dol.c
)
target_include_directories(test_dol PRIVATE src)
add_test(NAME dol COMMAND test_dol)

add_executable(test_rel
    tests/test_rel.c
    src/frontend/rel.c
)
target_include_directories(test_rel PRIVATE src)
add_test(NAME rel COMMAND test_rel)

add_executable(test_rpx
    tests/test_rpx.c
    src/frontend/rpx.c
)
target_include_directories(test_rpx PRIVATE src)
if(ZLIB_FOUND)
    target_compile_definitions(test_rpx PRIVATE DOLRECOMP_HAVE_ZLIB)
    target_link_libraries(test_rpx PRIVATE ZLIB::ZLIB)
endif()
add_test(NAME rpx COMMAND test_rpx)

add_executable(test_disc_extract
    tests/test_disc_extract.c
)
target_include_directories(test_disc_extract PRIVATE src)
add_test(NAME disc_extract
    COMMAND test_disc_extract
        $<SHELL_PATH:$<TARGET_FILE:dolrecomp>>
        $<SHELL_PATH:${CMAKE_CURRENT_BINARY_DIR}/disc_extract_test>
)

# PC-side CPU behavior checks.
add_executable(test_pc_reference
    tests/test_pc_reference.c
    src/frontend/decoder.c
    src/frontend/dol.c
    src/core/cpu.c
)
target_include_directories(test_pc_reference PRIVATE src)
if(NOT WIN32)
    target_link_libraries(test_pc_reference PRIVATE m)
endif()
add_test(NAME pc_reference COMMAND test_pc_reference)

# Generate C for the current opcode set and make sure the host compiler accepts it.
add_executable(test_codegen_emit
    tests/test_codegen_emit.c
    src/frontend/decoder.c
    src/backend/emitter.c
)
target_include_directories(test_codegen_emit PRIVATE src)
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
