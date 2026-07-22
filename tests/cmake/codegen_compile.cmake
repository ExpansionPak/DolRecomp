execute_process(
    COMMAND "${GENERATOR_EXE}" "${OUTPUT_C}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
    message(FATAL_ERROR "codegen generation failed:\n${gen_stdout}\n${gen_stderr}")
endif()

file(READ "${OUTPUT_C}" generated_source)
if(generated_source MATCHES "unknown instruction fallback")
    message(FATAL_ERROR "known opcode codegen emitted the unknown-instruction fallback")
endif()
if(NOT generated_source MATCHES "u32 target = ctx->lr & ~3u;[^}]*ctx->lr = 0x8000401Cu;[^}]*ctx->pc = target;")
    message(FATAL_ERROR "blrl codegen does not preserve the old LR branch target")
endif()
if(NOT generated_source MATCHES "#ifndef RECOMP_GENERATED_H")
    message(FATAL_ERROR "generated header has no include guard")
endif()
if(NOT generated_source MATCHES "#ifndef DOLRECOMP_CPU_HEADER")
    message(FATAL_ERROR "generated header does not expose the runtime CPU header contract")
endif()
if(NOT generated_source MATCHES "ctx->downcount -=")
    message(FATAL_ERROR "generated code has no guest cycle charges")
endif()
if(NOT generated_source MATCHES "if \\(!ppc_fp_available\\(ctx, 0x8000317Cu\\)\\) return;")
    message(FATAL_ERROR "generated floating-point code has no MSR FP gate")
endif()
if(NOT generated_source MATCHES "ppc_fallback_instruction\\(ctx, 0x7C13A0ACu")
    message(FATAL_ERROR "dcbf codegen does not route through the environment fallback")
endif()

get_filename_component(output_dir "${OUTPUT_C}" DIRECTORY)
set(check_src_dir "${output_dir}/codegen_check_project")
set(check_build_dir "${output_dir}/codegen_check_build")

file(TO_CMAKE_PATH "${OUTPUT_C}" output_c_cmake)
file(TO_CMAKE_PATH "${REPO_SRC}" repo_src_cmake)

file(MAKE_DIRECTORY "${check_src_dir}")
file(WRITE "${check_src_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)
project(CodegenCompileCheck C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
file(WRITE \"${check_src_dir}/host_cpu.h\" \"#include \\\"cpu/cpu.h\\\"\\n\")
add_library(codegen_check OBJECT \"${output_c_cmake}\")
target_include_directories(codegen_check PRIVATE \"${check_src_dir}\" \"${repo_src_cmake}\")
target_compile_definitions(codegen_check PRIVATE DOLRECOMP_CPU_HEADER=\"host_cpu.h\")
")

set(configure_args -S "${check_src_dir}" -B "${check_build_dir}")
if(DEFINED HOST_GENERATOR AND NOT HOST_GENERATOR STREQUAL "")
    list(APPEND configure_args -G "${HOST_GENERATOR}")
endif()
if(DEFINED HOST_GENERATOR_PLATFORM AND NOT HOST_GENERATOR_PLATFORM STREQUAL "")
    list(APPEND configure_args -A "${HOST_GENERATOR_PLATFORM}")
endif()
if(DEFINED HOST_GENERATOR_TOOLSET AND NOT HOST_GENERATOR_TOOLSET STREQUAL "")
    list(APPEND configure_args -T "${HOST_GENERATOR_TOOLSET}")
endif()
if(DEFINED HOST_C_COMPILER AND NOT HOST_C_COMPILER STREQUAL "")
    list(APPEND configure_args "-DCMAKE_C_COMPILER=${HOST_C_COMPILER}")
endif()
if(DEFINED HOST_BUILD_CONFIG
        AND NOT HOST_BUILD_CONFIG STREQUAL ""
        AND NOT HOST_GENERATOR MATCHES "Visual Studio|Xcode|Ninja Multi-Config")
    list(APPEND configure_args "-DCMAKE_BUILD_TYPE=${HOST_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${configure_args}
    RESULT_VARIABLE configure_result
    OUTPUT_VARIABLE configure_stdout
    ERROR_VARIABLE configure_stderr
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "codegen configure failed:\n${configure_stdout}\n${configure_stderr}")
endif()

set(build_args --build "${check_build_dir}")
if(DEFINED HOST_BUILD_CONFIG AND NOT HOST_BUILD_CONFIG STREQUAL "")
    list(APPEND build_args --config "${HOST_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${build_args}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "generated code did not compile:\n${build_stdout}\n${build_stderr}")
endif()
