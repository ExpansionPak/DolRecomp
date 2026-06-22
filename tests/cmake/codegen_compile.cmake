execute_process(
    COMMAND "${GENERATOR_EXE}" "${OUTPUT_C}"
    RESULT_VARIABLE gen_result
    OUTPUT_VARIABLE gen_stdout
    ERROR_VARIABLE gen_stderr
)
if(NOT gen_result EQUAL 0)
    message(FATAL_ERROR "codegen smoke generation failed:\n${gen_stdout}\n${gen_stderr}")
endif()

get_filename_component(output_dir "${OUTPUT_C}" DIRECTORY)
set(smoke_src_dir "${output_dir}/codegen_smoke_project")
set(smoke_build_dir "${output_dir}/codegen_smoke_build")

file(TO_CMAKE_PATH "${OUTPUT_C}" output_c_cmake)
file(TO_CMAKE_PATH "${REPO_SRC}" repo_src_cmake)

file(MAKE_DIRECTORY "${smoke_src_dir}")
file(WRITE "${smoke_src_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)
project(CodegenSmoke C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_library(codegen_smoke OBJECT \"${output_c_cmake}\")
target_include_directories(codegen_smoke PRIVATE \"${repo_src_cmake}\")
")

set(configure_args -S "${smoke_src_dir}" -B "${smoke_build_dir}")
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
    message(FATAL_ERROR "codegen smoke configure failed:\n${configure_stdout}\n${configure_stderr}")
endif()

set(build_args --build "${smoke_build_dir}")
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
