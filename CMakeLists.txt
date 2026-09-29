cmake_minimum_required(VERSION 3.16)
project(DolRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(DOLRECOMP_ENABLE_LLVM "Build the LLVM object backend" OFF)
option(DOLRECOMP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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
    if(DOLRECOMP_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    if(DOLRECOMP_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()
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

add_library(dr_ir STATIC
    src/ir/dolir.c
    src/ir/dolir_address.c
    src/ir/dolir_effects.c
    src/ir/dolir_builder.c
)
target_include_directories(dr_ir PUBLIC ${DOLRECOMP_SRC})
target_link_libraries(dr_ir PUBLIC dr_frontend)

if(DOLRECOMP_ENABLE_LLVM)
    add_library(dr_llvm STATIC
        src/backend/llvm/llvm_backend.cpp
        src/backend/llvm/target.cpp
        src/backend/llvm/passes.cpp
        src/backend/llvm/branch_targets.cpp
        src/backend/llvm/control_flow.cpp
        src/backend/llvm/emitter.cpp
        src/backend/llvm/native_wrapper.cpp
        src/backend/llvm/modern_wrapper.cpp
        src/backend/llvm/modern_state.cpp
        src/backend/llvm/native_abi.cpp
        src/backend/llvm/abi_liveness.cpp
        src/backend/llvm/abi_policy.cpp
        src/backend/llvm/call_liveness.cpp
        src/backend/llvm/abi_stats.cpp
        src/backend/llvm/cache_control.cpp
        src/backend/llvm/instruction_service.cpp
        src/backend/llvm/memory.cpp
        src/backend/llvm/memory_store.cpp
        src/backend/llvm/memory_service.cpp
        src/backend/llvm/fifo_service.cpp
        src/backend/llvm/memory_fallback.cpp
        src/backend/llvm/register_state.cpp
        src/backend/llvm/state_access.cpp
        src/backend/llvm/state_write.cpp
        src/backend/llvm/state_controls.cpp
        src/backend/llvm/exits.cpp
        src/backend/llvm/exit_materialization.cpp
        src/backend/llvm/timing.cpp
        src/backend/llvm/native_exits.cpp
        src/backend/llvm/instructions.cpp
        src/backend/llvm/regions.cpp
        src/backend/llvm/runtime.cpp
        src/backend/llvm/floating_point.cpp
        src/backend/llvm/fp_fixups.cpp
        src/backend/llvm/fpr_state.cpp
        src/backend/llvm/paired_single.cpp
        src/backend/llvm/paired_fma.cpp
        src/backend/llvm/psq_convert.cpp
        src/backend/llvm/psq.cpp
        src/backend/llvm/psq_load.cpp
        src/backend/llvm/psq_static.cpp
        src/backend/llvm/special_instructions.cpp
    )
    target_include_directories(dr_llvm PUBLIC ${DOLRECOMP_SRC})
    target_include_directories(dr_llvm SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(dr_llvm PUBLIC DOLRECOMP_ENABLE_LLVM=1)
    target_compile_definitions(dr_llvm PRIVATE ${LLVM_DEFINITIONS})
    target_link_libraries(dr_llvm PUBLIC dr_ir)
    
    set(DOLRECOMP_LLVM_COMPONENTS
        Core
        Support
        Analysis
        BitWriter
        Passes
        Target
        MC
        native
        nativecodegen
        # Target backends are appended below, gated on what this LLVM ships.
    )

    # target.cpp registers only the targets it can emit for, and only those
    # this LLVM was built with. Naming them unconditionally breaks two ways:
    # against an LLVM without one of them the call does not even compile, and
    # registering every target via InitializeAll* drags unused backends into
    # the link. LLVM_TARGETS_TO_BUILD is what LLVMConfig.cmake reports.
    if("X86" IN_LIST LLVM_TARGETS_TO_BUILD)
        list(APPEND DOLRECOMP_LLVM_COMPONENTS X86CodeGen X86AsmParser)
        target_compile_definitions(dr_llvm PRIVATE DOLLLVM_HAVE_X86_TARGET=1)
    endif()
    if("AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
        list(APPEND DOLRECOMP_LLVM_COMPONENTS AArch64CodeGen AArch64AsmParser)
        target_compile_definitions(dr_llvm PRIVATE DOLLLVM_HAVE_AARCH64_TARGET=1)
    endif()
    if(NOT "X86" IN_LIST LLVM_TARGETS_TO_BUILD AND NOT "AArch64" IN_LIST LLVM_TARGETS_TO_BUILD)
        message(FATAL_ERROR
            "DolRecomp's LLVM backend needs the X86 or AArch64 target; this LLVM has: ${LLVM_TARGETS_TO_BUILD}")
    endif()

    if(TARGET LLVM)
        target_link_libraries(dr_llvm PRIVATE LLVM)
    elseif(MINGW)
        find_library(DOLRECOMP_LLVM_SHARED_IMPORT
            NAMES LLVM-${LLVM_VERSION_MAJOR} LLVM
            HINTS ${LLVM_LIBRARY_DIRS}
            NO_DEFAULT_PATH)
        if(DOLRECOMP_LLVM_SHARED_IMPORT MATCHES "[.]dll[.]a$")
            target_link_libraries(dr_llvm PRIVATE "${DOLRECOMP_LLVM_SHARED_IMPORT}")
        else()
            llvm_map_components_to_libnames(DOLRECOMP_LLVM_LIBS ${DOLRECOMP_LLVM_COMPONENTS})
            target_link_libraries(dr_llvm PRIVATE ${DOLRECOMP_LLVM_LIBS})
        endif()
    else()
        llvm_map_components_to_libnames(DOLRECOMP_LLVM_LIBS ${DOLRECOMP_LLVM_COMPONENTS})
        target_link_libraries(dr_llvm PRIVATE ${DOLRECOMP_LLVM_LIBS})
    endif()
endif()

add_library(dr_analysis STATIC
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
    src/backend/native_state.c
    src/backend/native_module.c
    src/backend/symbols.c
    src/backend/module_abi.c
    src/backend/variant_output.c
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
# The generated-object cache must invalidate when the code that generates those
# objects changes. A hand-edited version string cannot do that -- it only
# invalidates when somebody remembers to bump it. Hash the sources that decide
# what gets emitted and fold the digest into every cache key. GLOB with
# CONFIGURE_DEPENDS so a newly added emitter file is covered automatically
# rather than needing to be added to a list that can rot.
file(GLOB DOLRECOMP_CODEGEN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/llvm/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/llvm/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ir/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ir/*.h")
list(SORT DOLRECOMP_CODEGEN_SOURCES)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${DOLRECOMP_CODEGEN_SOURCES})
set(DOLRECOMP_CODEGEN_DIGEST "")
foreach(_codegen_source IN LISTS DOLRECOMP_CODEGEN_SOURCES)
    file(SHA256 "${_codegen_source}" _codegen_file_hash)
    string(APPEND DOLRECOMP_CODEGEN_DIGEST "${_codegen_file_hash}")
endforeach()
string(SHA256 DOLRECOMP_CODEGEN_SOURCE_HASH "${DOLRECOMP_CODEGEN_DIGEST}")
list(LENGTH DOLRECOMP_CODEGEN_SOURCES _codegen_count)
message(STATUS "Codegen source digest: ${DOLRECOMP_CODEGEN_SOURCE_HASH} (${_codegen_count} files)")
target_compile_definitions(dr_app PRIVATE
    DOLRECOMP_CODEGEN_SOURCE_HASH="${DOLRECOMP_CODEGEN_SOURCE_HASH}")
target_link_libraries(dr_app PUBLIC dr_backend dr_analysis dr_frontend dr_platform dr_ir)
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

add_executable(test_cli tests/test_cli.c)
target_link_libraries(test_cli PRIVATE dr_app)
if(DOLRECOMP_ENABLE_LLVM)
    set_property(TARGET test_cli PROPERTY LINKER_LANGUAGE CXX)
endif()

add_executable(dolir_stats tools/dolir_stats.c)
target_link_libraries(dolir_stats PRIVATE dr_ir dr_frontend dr_analysis)

enable_testing()

add_test(NAME cli COMMAND test_cli)

add_test(NAME source_policy
    COMMAND ${CMAKE_COMMAND}
        -DREPO_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/check_source_policy.cmake)

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

add_executable(test_module_abi tests/test_module_abi.c)
target_link_libraries(test_module_abi PRIVATE dr_backend dr_cpu)
add_test(NAME module_abi COMMAND test_module_abi)

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

if(DOLRECOMP_ENABLE_LLVM)
    add_executable(llvm_fixture tests/llvm_fixture.cpp)
    target_link_libraries(llvm_fixture PRIVATE dr_llvm)
    add_test(NAME llvm_fixture COMMAND llvm_fixture
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.o
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll)
    add_executable(llvm_ir_test tests/llvm_ir_test.cpp)
    target_include_directories(llvm_ir_test SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(llvm_ir_test PRIVATE ${LLVM_DEFINITIONS})
    target_link_libraries(llvm_ir_test PRIVATE dr_llvm)
    add_test(NAME llvm_ir COMMAND llvm_ir_test
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.instrumented
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.pgo
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.o.v2.bc
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.fast
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.abi.caller
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.a57
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.state-memory)
    set_tests_properties(llvm_ir PROPERTIES DEPENDS llvm_fixture)
    add_executable(modern_codegen_test tests/modern_codegen_test.cpp)
    target_include_directories(modern_codegen_test SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(modern_codegen_test PRIVATE ${LLVM_DEFINITIONS})
    target_link_libraries(modern_codegen_test PRIVATE dr_llvm)
    add_test(NAME modern_codegen COMMAND modern_codegen_test
             ${CMAKE_CURRENT_BINARY_DIR}/dolir_test.ll.native)
    set_tests_properties(modern_codegen PROPERTIES DEPENDS llvm_fixture)
    set(DOLRECOMP_TEST_OBJECT ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o)
    set(DOLRECOMP_TEST_IR ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.ll)
    set(DOLRECOMP_ABI_CALLER_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o.abi.caller)
    set(DOLRECOMP_ABI_CALLEE_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o.abi.callee)
    set(DOLRECOMP_ABI_LOOP_CALLER_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o.abi.loop.caller)
    set(DOLRECOMP_ABI_LOOP_CALLEE_OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/dolir_execute.o.abi.loop.callee)
    set(DOLRECOMP_COMPACT_OBJECT ${DOLRECOMP_TEST_OBJECT}.compact)
    set(DOLRECOMP_CONTEXT_OBJECT ${DOLRECOMP_TEST_OBJECT}.context)
    add_custom_command(
        OUTPUT ${DOLRECOMP_TEST_OBJECT} ${DOLRECOMP_TEST_IR}
        BYPRODUCTS
            ${DOLRECOMP_TEST_OBJECT}.v2
            ${DOLRECOMP_TEST_OBJECT}.v2.copy
            ${DOLRECOMP_TEST_OBJECT}.v2.bc
            ${DOLRECOMP_TEST_OBJECT}.v2.copy.bc
            ${DOLRECOMP_TEST_OBJECT}.v3
            ${DOLRECOMP_TEST_OBJECT}.arm64
            ${DOLRECOMP_TEST_OBJECT}.a57
            ${DOLRECOMP_TEST_IR}.a57
            ${DOLRECOMP_TEST_OBJECT}.instrumented
            ${DOLRECOMP_TEST_IR}.instrumented
            ${DOLRECOMP_TEST_OBJECT}.pgo
            ${DOLRECOMP_TEST_IR}.pgo
            ${DOLRECOMP_TEST_OBJECT}.fast
            ${DOLRECOMP_TEST_IR}.fast
            ${DOLRECOMP_ABI_CALLER_OBJECT}
            ${DOLRECOMP_ABI_CALLEE_OBJECT}
            ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}
            ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}
            ${DOLRECOMP_TEST_IR}.abi.caller
            ${DOLRECOMP_TEST_OBJECT}.state-memory
            ${DOLRECOMP_TEST_IR}.state-memory
            ${DOLRECOMP_TEST_OBJECT}.native
            ${DOLRECOMP_TEST_IR}.native
            ${DOLRECOMP_COMPACT_OBJECT}
            ${DOLRECOMP_CONTEXT_OBJECT}
            ${DOLRECOMP_ABI_CALLER_OBJECT}.compact
            ${DOLRECOMP_ABI_CALLEE_OBJECT}.compact
            ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.compact
            ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.compact
            ${DOLRECOMP_ABI_CALLER_OBJECT}.context
            ${DOLRECOMP_ABI_CALLEE_OBJECT}.context
            ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.context
            ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.context
        COMMAND llvm_fixture ${DOLRECOMP_TEST_OBJECT} ${DOLRECOMP_TEST_IR}
        DEPENDS llvm_fixture
        VERBATIM
    )
    add_custom_target(llvm_fixture_outputs
        DEPENDS ${DOLRECOMP_TEST_OBJECT} ${DOLRECOMP_TEST_IR})
    set_source_files_properties(${DOLRECOMP_TEST_OBJECT}
        PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    set_source_files_properties(
        ${DOLRECOMP_TEST_OBJECT}.native
        ${DOLRECOMP_ABI_CALLER_OBJECT}
        ${DOLRECOMP_ABI_CALLEE_OBJECT}
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}
        ${DOLRECOMP_COMPACT_OBJECT}
        ${DOLRECOMP_CONTEXT_OBJECT}
        ${DOLRECOMP_ABI_CALLER_OBJECT}.compact
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.compact
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.compact
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.compact
        ${DOLRECOMP_ABI_CALLER_OBJECT}.context
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.context
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.context
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.context
        PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    add_executable(modern_memory_service_test
        tests/modern_memory_service_test.c ${DOLRECOMP_TEST_OBJECT}.native)
    add_dependencies(modern_memory_service_test llvm_fixture_outputs)
    if(NOT WIN32)
        target_link_libraries(modern_memory_service_test PRIVATE m)
    endif()
    add_test(NAME modern_memory_service COMMAND modern_memory_service_test)
    set_tests_properties(modern_memory_service PROPERTIES DEPENDS llvm_fixture)
    add_executable(llvm_smoke_test tests/llvm_smoke_test.c ${DOLRECOMP_TEST_OBJECT})
    target_link_libraries(llvm_smoke_test PRIVATE dr_cpu)
    add_test(NAME llvm_smoke COMMAND llvm_smoke_test)
    add_executable(llvm_semantics_test
        tests/llvm_semantics_test.c ${DOLRECOMP_TEST_OBJECT})
    target_link_libraries(llvm_semantics_test PRIVATE dr_cpu)
    add_test(NAME llvm_semantics COMMAND llvm_semantics_test)
    add_executable(native_interception_test
        tests/native_interception_test.c ${DOLRECOMP_TEST_OBJECT})
    target_link_libraries(native_interception_test PRIVATE dr_cpu)
    add_test(NAME native_interception COMMAND native_interception_test)
    add_executable(llvm_abi_execute_test
        tests/llvm_abi_execute_test.c
        ${DOLRECOMP_ABI_CALLER_OBJECT}
        ${DOLRECOMP_ABI_CALLEE_OBJECT})
    add_dependencies(llvm_abi_execute_test llvm_fixture_outputs)
    target_link_libraries(llvm_abi_execute_test PRIVATE dr_cpu)
    add_test(NAME llvm_abi_execute COMMAND llvm_abi_execute_test)
    add_executable(llvm_abi_execute_compact_test
        tests/llvm_abi_execute_test.c
        ${DOLRECOMP_ABI_CALLER_OBJECT}.compact
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.compact)
    add_dependencies(llvm_abi_execute_compact_test llvm_fixture_outputs)
    target_link_libraries(llvm_abi_execute_compact_test PRIVATE dr_cpu)
    add_test(NAME llvm_abi_execute_compact COMMAND llvm_abi_execute_compact_test)
    add_executable(llvm_abi_execute_context_test
        tests/llvm_abi_execute_test.c
        ${DOLRECOMP_ABI_CALLER_OBJECT}.context
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.context)
    add_dependencies(llvm_abi_execute_context_test llvm_fixture_outputs)
    target_link_libraries(llvm_abi_execute_context_test PRIVATE dr_cpu)
    add_test(NAME llvm_abi_execute_context COMMAND llvm_abi_execute_context_test)
    add_executable(paired_single_test
        tests/paired_single_test.c ${DOLRECOMP_TEST_OBJECT})
    target_link_libraries(paired_single_test PRIVATE dr_cpu)
    add_test(NAME paired_single COMMAND paired_single_test)
    add_executable(llvm_backend_bench
        benchmarks/llvm_backend_bench.c
        ${DOLRECOMP_TEST_OBJECT}
        ${DOLRECOMP_ABI_CALLER_OBJECT}
        ${DOLRECOMP_ABI_CALLEE_OBJECT}
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT})
    add_dependencies(llvm_backend_bench llvm_fixture_outputs)
    target_link_libraries(llvm_backend_bench PRIVATE dr_cpu)
    add_executable(llvm_backend_bench_compact
        benchmarks/llvm_backend_bench.c
        ${DOLRECOMP_COMPACT_OBJECT}
        ${DOLRECOMP_ABI_CALLER_OBJECT}.compact
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.compact
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.compact
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.compact)
    add_dependencies(llvm_backend_bench_compact llvm_fixture_outputs)
    target_link_libraries(llvm_backend_bench_compact PRIVATE dr_cpu)
    add_executable(llvm_backend_bench_context
        benchmarks/llvm_backend_bench.c
        ${DOLRECOMP_CONTEXT_OBJECT}
        ${DOLRECOMP_ABI_CALLER_OBJECT}.context
        ${DOLRECOMP_ABI_CALLEE_OBJECT}.context
        ${DOLRECOMP_ABI_LOOP_CALLER_OBJECT}.context
        ${DOLRECOMP_ABI_LOOP_CALLEE_OBJECT}.context)
    add_dependencies(llvm_backend_bench_context llvm_fixture_outputs)
    target_link_libraries(llvm_backend_bench_context PRIVATE dr_cpu)
    find_program(DOLRECOMP_LLVM_OBJDUMP
        NAMES llvm-objdump-${LLVM_VERSION_MAJOR} llvm-objdump
        HINTS ${LLVM_TOOLS_BINARY_DIR})
    if(DOLRECOMP_LLVM_OBJDUMP)
        add_test(NAME llvm_codegen
            COMMAND ${CMAKE_COMMAND}
                -DOBJDUMP=${DOLRECOMP_LLVM_OBJDUMP}
                -DX86_OBJECT=${DOLRECOMP_TEST_OBJECT}.v3
                -DARM_OBJECT=${DOLRECOMP_TEST_OBJECT}.a57
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/check_llvm_codegen.cmake)
        set_tests_properties(llvm_codegen PROPERTIES DEPENDS llvm_fixture)
    endif()
    add_executable(llvm_pipeline_test tests/llvm_pipeline_test.c)
    target_include_directories(llvm_pipeline_test PRIVATE ${DOLRECOMP_SRC})
    add_test(NAME llvm_pipeline COMMAND llvm_pipeline_test
             $<TARGET_FILE:dolrecomp>
             ${CMAKE_CURRENT_BINARY_DIR}/llvm_pipeline)
    add_test(NAME llvm_generated_compile
        COMMAND ${CMAKE_COMMAND}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}
            -DGENERATED_C=${CMAKE_CURRENT_BINARY_DIR}/llvm_pipeline/out/generated/generated.c
            -DREPO_SRC=${CMAKE_CURRENT_SOURCE_DIR}/src
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/compile_generated_llvm.cmake)
    set_tests_properties(llvm_generated_compile PROPERTIES DEPENDS llvm_pipeline)
    if(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        add_test(NAME llvm_variant_runtime
            COMMAND ${CMAKE_COMMAND}
                -DC_COMPILER=${CMAKE_C_COMPILER}
                "-DC_FLAGS=${CMAKE_C_FLAGS}"
                -DGENERATED_DIR=${CMAKE_CURRENT_BINARY_DIR}/llvm_pipeline/out/generated
                -DCPU_LIBRARY=$<TARGET_FILE:dr_cpu>
                -DBACKEND_LIBRARY=$<TARGET_FILE:dr_backend>
                -DREPO_SRC=${CMAKE_CURRENT_SOURCE_DIR}/src
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/link_generated_variants.cmake)
        set_tests_properties(llvm_variant_runtime PROPERTIES DEPENDS llvm_pipeline)
    endif()
endif()
