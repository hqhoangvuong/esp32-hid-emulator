cmake_minimum_required(VERSION 3.20)
project(host_tools C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_testing()

# Portable firmware modules live in main/ and are built here against the simulator backend.
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../main)
add_compile_definitions(MACROS_DIR="${FW}/macros" _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

add_library(core STATIC
    sim/hid_out_sim.c
    ${FW}/keys.c
    ${FW}/rhythm.c
    ${FW}/pointer.c
    ${FW}/typing.c
    sim/editor_model.c
)
target_include_directories(core PUBLIC ${FW} sim)
if(NOT MSVC)
    target_link_libraries(core PUBLIC m)
endif()

add_subdirectory(sim)
