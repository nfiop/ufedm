cmake_minimum_required(VERSION 3.20)

add_executable(testd main.c ecc-sw-hamming.c
    ${PROJECT_SOURCE_DIR}/user/common/proxy_dev.c)

target_include_directories(testd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/user/common
)

target_compile_options(testd PRIVATE -Wall -Wextra -Werror)
target_link_options(testd PRIVATE -static)

# Optional: debug build support
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(testd PRIVATE -O0 -g)
endif()

