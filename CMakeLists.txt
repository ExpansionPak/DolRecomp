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
    src/runtime/runtime.c
)

target_include_directories(dolrecomp PRIVATE src)

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
