cmake_minimum_required(VERSION 3.20)

set(PROXY_UTESTS
    test_proxy_ioctls
    test_proxy_mmap
)

foreach(t ${PROXY_UTESTS})
    add_executable(${t} ${t}.c)
    
    add_dependencies(${t} ufedmdev-io)

    # FIXME: Consider using the exposed setting from the ufedmdev-io.cmake file itself
    target_include_directories(${t} PRIVATE
        ${PROJECT_SOURCE_DIR}/libs/ufedmdev-io
    )

    target_link_libraries(${t} PRIVATE
        ${UFEDMDEV_IO_LIBRARY}
    )

    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)

    target_link_options(${t} PRIVATE -static)

    # Optional: debug build support
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${t} PRIVATE -O0 -g)
    endif()
endforeach()

