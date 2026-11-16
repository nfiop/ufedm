cmake_minimum_required(VERSION 3.20)

include(${PROJECT_SOURCE_DIR}/libs/ufedm-dev.cmake)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/utests)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/testd)
