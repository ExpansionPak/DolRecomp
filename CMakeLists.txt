cmake_minimum_required(VERSION 3.16)
project(DolRecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# just warnings, nothing crazy
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(dolrecomp
    src/main.c
    src/frontend/dol.c
    src/frontend/decoder.c
    src/backend/emitter.c
)

target_include_directories(dolrecomp PRIVATE src)
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

# PC-side behavior checks against Dolphin-derived expectations
add_executable(test_pc_reference
    tests/test_pc_reference.c
    src/frontend/decoder.c
    src/frontend/dol.c
    src/core/cpu.c
)
target_include_directories(test_pc_reference PRIVATE src)
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
        -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/codegen_smoke.c
        -DOUTPUT_OBJ=${CMAKE_CURRENT_BINARY_DIR}/codegen_smoke.c.o
        -DCC=${CMAKE_C_COMPILER}
        -DCC_ID=${CMAKE_C_COMPILER_ID}
        -DREPO_SRC=${CMAKE_CURRENT_SOURCE_DIR}/src
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/codegen_compile.cmake
)
