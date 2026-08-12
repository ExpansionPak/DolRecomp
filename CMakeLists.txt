cmake_minimum_required(VERSION 3.16)
project(DolRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(DOLRECOMP_ENABLE_LLVM "Build the x86-64 LLVM object backend" OFF)

# Discover LLVM before creating targets so its MSVC runtime choice is uniform.
if(DOLRECOMP_ENABLE_LLVM)
    enable_language(CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(LLVM CONFIG REQUIRED)
    if(LLVM_PACKAGE_VERSION VERSION_LESS 19 OR LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 21)
        message(FATAL_ERROR "DolRecomp LLVM backend requires LLVM 19 or 20 (found ${LLVM_PACKAGE_VERSION})")
    endif()
endif()

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

add_library(dr_common STATIC src/common/perf.c)
target_include_directories(dr_common PUBLIC ${DOLRECOMP_SRC})

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

add_library(dr_ir STATIC
    src/ir/dolir.c
    src/ir/dolir_builder.c
)
target_include_directories(dr_ir PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_ir PUBLIC dr_frontend)

if(DOLRECOMP_ENABLE_LLVM)
    add_library(dr_llvm STATIC
        src/backend/llvm/llvm_backend.cpp
        src/backend/llvm/llvm_control_flow.cpp
        src/backend/llvm/llvm_function_emitter.cpp
        src/backend/llvm/llvm_memory_lowering.cpp
        src/backend/llvm/llvm_runtime_lowering.cpp
    )
    target_include_directories(dr_llvm PUBLIC ${DOLRECOMP_SRC})
    target_include_directories(dr_llvm SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(dr_llvm PUBLIC DOLRECOMP_ENABLE_LLVM=1)
    target_compile_definitions(dr_llvm PRIVATE ${LLVM_DEFINITIONS})
    target_link_libraries(dr_llvm PUBLIC dr_ir)
    if(MINGW)
        find_library(DOLRECOMP_LLVM_SHARED_IMPORT
            NAMES LLVM-${LLVM_VERSION_MAJOR} LLVM
            HINTS ${LLVM_LIBRARY_DIRS}
            NO_DEFAULT_PATH)
        if(DOLRECOMP_LLVM_SHARED_IMPORT MATCHES "[.]dll[.]a$")
            target_link_libraries(dr_llvm PRIVATE
                "${DOLRECOMP_LLVM_SHARED_IMPORT}")
        else()
            llvm_map_components_to_libnames(DOLRECOMP_LLVM_LIBS
                Core Support Analysis Passes Target MC native nativecodegen)
            target_link_libraries(dr_llvm PRIVATE ${DOLRECOMP_LLVM_LIBS})
        endif()
    elseif(TARGET LLVM)
        target_link_libraries(dr_llvm PRIVATE LLVM)
    else()
        llvm_map_components_to_libnames(DOLRECOMP_LLVM_LIBS
            Core Support Analysis Passes Target MC native nativecodegen)
        target_link_libraries(dr_llvm PRIVATE ${DOLRECOMP_LLVM_LIBS})
    endif()
endif()

add_library(dr_analysis STATIC
    src/analysis/cfg.c
    src/analysis/embedded_data.c
    src/analysis/smc.c
    src/analysis/symbol_map.c
)
target_include_directories(dr_analysis PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_analysis PUBLIC dr_frontend)

add_library(dr_backend STATIC
    src/backend/c_cfg.c
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
target_link_libraries(dr_app PUBLIC dr_backend dr_analysis dr_frontend dr_platform dr_ir dr_common)
if(DOLRECOMP_ENABLE_LLVM)
    target_link_libraries(dr_app PUBLIC dr_llvm)
endif()

add_executable(dolrecomp src/app/main.c)
target_link_libraries(dolrecomp PRIVATE dr_app)
if(DOLRECOMP_ENABLE_LLVM)
    set_property(TARGET dolrecomp PROPERTY LINKER_LANGUAGE CXX)
    # Large LLVM modules exceed MSVC's default 1 MiB stack.
    if(MSVC)
        target_link_options(dolrecomp PRIVATE /STACK:8388608)
    endif()
endif()

add_executable(dolir_stats tools/dolir_stats.c)
target_link_libraries(dolir_stats PRIVATE dr_ir dr_frontend dr_analysis)

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

add_executable(test_c_cfg tests/test_c_cfg.c)
target_link_libraries(test_c_cfg PRIVATE dr_backend)
add_test(NAME c_cfg COMMAND test_c_cfg)

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

set(DOLRECOMP_C_TEST_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/c_execute_generated.c)
add_custom_command(
    OUTPUT ${DOLRECOMP_C_TEST_SOURCE}
    COMMAND test_codegen_emit ${DOLRECOMP_C_TEST_SOURCE}
    DEPENDS test_codegen_emit
    VERBATIM
)
add_executable(test_c_execute
    tests/test_c_execute.c
    ${DOLRECOMP_C_TEST_SOURCE}
)
target_link_libraries(test_c_execute PRIVATE dr_cpu)
add_test(NAME c_execute COMMAND test_c_execute)

add_executable(test_fpscr tests/test_fpscr.c)
target_link_libraries(test_fpscr PRIVATE dr_cpu)
add_test(NAME fpscr COMMAND test_fpscr)

add_executable(test_float_semantics tests/test_float_semantics.c)
target_link_libraries(test_float_semantics PRIVATE dr_cpu)
add_test(NAME float_semantics COMMAND test_float_semantics)

add_executable(test_dolir tests/test_dolir.c)
target_link_libraries(test_dolir PRIVATE dr_ir)
add_test(NAME dolir COMMAND test_dolir)

add_executable(test_cfg tests/test_cfg.c)
target_link_libraries(test_cfg PRIVATE dr_analysis)
add_test(NAME cfg COMMAND test_cfg)

add_executable(test_perf tests/test_perf.c)
target_link_libraries(test_perf PRIVATE dr_common)
add_test(NAME perf COMMAND test_perf ${CMAKE_CURRENT_BINARY_DIR}/perf_test)

if(DOLRECOMP_ENABLE_LLVM)
    add_executable(test_llvm_backend tests/test_llvm_backend.cpp)
    target_link_libraries(test_llvm_backend PRIVATE dr_llvm)
    add_test(NAME llvm_backend COMMAND test_llvm_backend
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.o
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll)
    set(DOLRECOMP_TEST_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o)
    set(DOLRECOMP_TEST_IR ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.ll)
    add_custom_command(
        OUTPUT ${DOLRECOMP_TEST_OBJECT} ${DOLRECOMP_TEST_IR}
        COMMAND test_llvm_backend ${DOLRECOMP_TEST_OBJECT} ${DOLRECOMP_TEST_IR}
        DEPENDS test_llvm_backend
        VERBATIM
    )
    set_source_files_properties(${DOLRECOMP_TEST_OBJECT}
        PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    add_executable(test_llvm_execute tests/test_llvm_execute.c ${DOLRECOMP_TEST_OBJECT})
    target_link_libraries(test_llvm_execute PRIVATE dr_cpu)
    add_test(NAME llvm_execute COMMAND test_llvm_execute)
    add_executable(test_llvm_pipeline tests/test_llvm_pipeline.c)
    target_include_directories(test_llvm_pipeline PRIVATE ${DOLRECOMP_SRC})
    add_test(NAME llvm_pipeline COMMAND test_llvm_pipeline
             $<TARGET_FILE:dolrecomp>
             ${CMAKE_CURRENT_BINARY_DIR}/llvm_pipeline)
endif()
