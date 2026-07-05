cmake_minimum_required(VERSION 3.20)

# --------------------------------------------------
# Buildroot vs Native (host) mode selection
# --------------------------------------------------
set(BUILDROOT_DIR "" CACHE PATH "Buildroot root directory")
set(TOOLCHAIN_PREFIX "" CACHE PATH "Toolchain architecture prefix")

set(KERNEL_MODULE_SRC ${CMAKE_SOURCE_DIR}/kernel)
set(KMOD_STAGE ${CMAKE_BINARY_DIR}/kmod)
set(STAMP ${KMOD_STAGE}/.stamp)

if(BUILDROOT_DIR STREQUAL "")
    set(NATIVE_MODE ON)
else()
    set(NATIVE_MODE OFF)
endif()

message(STATUS "Native mode: ${NATIVE_MODE}")

# --------------------------------------------------
# TOOLCHAIN SELECTION (MUST HAPPEN EARLY)
# --------------------------------------------------
if(NATIVE_MODE)

    message(STATUS "Using native toolchain (gcc/g++)")

    set(CMAKE_C_COMPILER gcc CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER g++ CACHE STRING "" FORCE)

    execute_process(
        COMMAND uname -m
        OUTPUT_VARIABLE TOOLCHAIN_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

else()

    message(STATUS "Using Buildroot toolchain")

    set(TOOLCHAIN_FILE
        "${BUILDROOT_DIR}/output/host/share/buildroot/toolchainfile.cmake"
    )

    if(NOT EXISTS "${TOOLCHAIN_FILE}")
        message(FATAL_ERROR "Buildroot toolchain file not found: ${TOOLCHAIN_FILE}")
    endif()

    set(CMAKE_TOOLCHAIN_FILE "${TOOLCHAIN_FILE}" CACHE FILEPATH "" FORCE)

    file(GLOB TOOLCHAINS "${BUILDROOT_DIR}/output/host/usr/bin/*-gcc")
    list(GET TOOLCHAINS 0 GCC)

    get_filename_component(GCC_NAME "${GCC}" NAME)
    string(REGEX REPLACE "-gcc$" "" TOOLCHAIN_PREFIX "${GCC_NAME}")

endif()

project(ufedm C)

message(STATUS "C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")

if(TOOLCHAIN_PREFIX MATCHES "x86_64")
    set(KERNEL_ARCH "x86")

elseif(TOOLCHAIN_PREFIX MATCHES "aarch64")
    set(KERNEL_ARCH "arm64")

elseif(TOOLCHAIN_PREFIX MATCHES "^arm")
    set(KERNEL_ARCH "arm")

elseif(TOOLCHAIN_PREFIX MATCHES "riscv64")
    set(KERNEL_ARCH "riscv")

else()
    message(FATAL_ERROR "Unknown toolchain prefix: ${TOOLCHAIN_PREFIX}")
endif()

if(KERNEL_ARCH STREQUAL "")
    message(FATAL_ERROR
        "KERNEL_ARCH is not set.\n"
        "Are you running on a non-supported architecture?")
endif()

if(NATIVE_MODE)

    message(STATUS "Building against native host kernel")

    execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_RELEASE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(LINUX_TREE "/lib/modules/${KERNEL_RELEASE}/build")

    if(NOT EXISTS "${LINUX_TREE}")
        message(FATAL_ERROR
            "Kernel build directory not found:\n"
            "  ${LINUX_TREE}\n"
            "Install the headers for your running kernel.")
    endif()

    message(STATUS "Native compile: ${KERNEL_ARCH}")

    set(KERNEL_MAKE_ARGS
        -C ${LINUX_TREE}
	    M=${KERNEL_MODULE_SRC}
        modules
    )

else()

    message(STATUS "Building against Buildroot")

    set(BR_OUTPUT "${BUILDROOT_DIR}/output")

    #
    # Find kernel tree
    #
    file(GLOB KERNEL_DIRS
        "${BR_OUTPUT}/build/linux-*")

    list(LENGTH KERNEL_DIRS NUM_KERNELS)

    if(NUM_KERNELS EQUAL 0)
        message(FATAL_ERROR "No linux-* directory found")
    endif()

    list(GET KERNEL_DIRS 0 LINUX_TREE)

    #
    # Find cross compiler
    #
    file(GLOB GCC_CANDIDATES
        "${BR_OUTPUT}/host/bin/*gcc")

    list(LENGTH GCC_CANDIDATES NUM_GCC)

    if(NUM_GCC EQUAL 0)
        message(FATAL_ERROR "No Buildroot gcc found")
    endif()

    list(GET GCC_CANDIDATES 0 GCC_PATH)

    string(REPLACE "gcc" "" CROSS_COMPILE "${GCC_PATH}")

    message(STATUS "Linux tree: ${LINUX_TREE}")
    message(STATUS "Cross compile: ${CROSS_COMPILE}")
    message(STATUS "Cross compile target: ${KERNEL_ARCH}")

    set(KERNEL_MAKE_ARGS
        -C ${LINUX_TREE}
	    M=${KERNEL_MODULE_SRC}
        ARCH=${KERNEL_ARCH}
        CROSS_COMPILE=${CROSS_COMPILE}
        modules
    )

endif()

add_custom_target(kernel_module ALL
    COMMAND ${CMAKE_MAKE_PROGRAM} ${KERNEL_MAKE_ARGS}
    
    COMMAND ${CMAKE_COMMAND} -E copy
    	    ${KERNEL_MODULE_SRC}/ufedm.ko
    	    ${KMOD_STAGE}/

    WORKING_DIRECTORY ${KERNEL_MODULE_SRC}
    VERBATIM
)

add_custom_target(kernel_module_clean
    COMMAND ${CMAKE_MAKE_PROGRAM}
	-C ${LINUX_TREE}
	M=${KERNEL_MODULE_SRC}
        clean

    WORKING_DIRECTORY ${KERNEL_MODULE_SRC}
    VERBATIM
)

add_subdirectory(user)
