cmake_minimum_required(VERSION 3.20)

project(ufedm C)

set(BUILDROOT_DIR "" CACHE PATH
    "Path to Buildroot root directory")

if(NOT DEFINED BUILDROOT_DIR OR BUILDROOT_DIR STREQUAL "")
    message(FATAL_ERROR
        "BUILDROOT_DIR is not set.\n"
        "Pass it with -DBUILDROOT_DIR=/path/to/buildroot")
endif()

if(NOT DEFINED KERNEL_ARCH OR KERNEL_ARCH STREQUAL "")
    message(FATAL_ERROR
        "KERNEL_ARCH is not set. See README.md for details.\n"
        "Pass it with -KERNEL_ARCH=arm for example")
endif()

set(BR_OUTPUT "${BUILDROOT_DIR}/output")

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")

# Find the kernel tree
file(GLOB KERNEL_DIRS
    "${BR_OUTPUT}/build/linux-*")

list(LENGTH KERNEL_DIRS NUM_KERNELS)

if(NUM_KERNELS EQUAL 0)
    message(FATAL_ERROR "No linux-* directory found")
endif()

list(GET KERNEL_DIRS 0 LINUX_TREE)

# Find the cross compiler
file(GLOB GCC_CANDIDATES
    "${BR_OUTPUT}/host/bin/*gcc")

list(LENGTH GCC_CANDIDATES NUM_GCC)

if(NUM_GCC EQUAL 0)
    message(FATAL_ERROR "No Buildroot gcc found")
endif()

list(GET GCC_CANDIDATES 0 GCC_PATH)

get_filename_component(GCC_NAME "${GCC_PATH}" NAME)

string(REPLACE "gcc" "" CROSS_COMPILE "${GCC_PATH}")

message(STATUS "Linux tree: ${LINUX_TREE}")
message(STATUS "Cross compile: ${CROSS_COMPILE}")

set(KMOD_OUT "${CMAKE_BINARY_DIR}/kmod")

add_custom_target(module ALL
    COMMAND ${CMAKE_MAKE_PROGRAM}
        -C ${LINUX_TREE}
        M=${CMAKE_CURRENT_SOURCE_DIR}
        MO=${KMOD_OUT}
        ARCH=${KERNEL_ARCH}
        CROSS_COMPILE=${CROSS_COMPILE}
        modules
)

add_subdirectory(utests)
