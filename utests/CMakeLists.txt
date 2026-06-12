cmake_minimum_required(VERSION 3.20)

set(UTESTS
    test_proxy_ioctls
)

foreach(t ${UTESTS})
    add_executable(${t} ${t}.c)

    target_include_directories(${t} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )

    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)

    # Optional: debug build support
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${t} PRIVATE -O0 -g)
    endif()
endforeach()

