cmake_minimum_required(VERSION 3.20)

set(PROXY_UTESTS
    test_proxy_ioctls
    test_proxy_mmap
)

foreach(t ${PROXY_UTESTS})
    add_executable(${t} ${t}.c 
        ${PROJECT_SOURCE_DIR}/user/common/proxy_dev.c)

    target_include_directories(${t} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/user/common
    )

    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)

    target_link_options(${t} PRIVATE -static)

    # Optional: debug build support
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${t} PRIVATE -O0 -g)
    endif()
endforeach()

