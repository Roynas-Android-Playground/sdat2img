cmake_minimum_required(VERSION 3.28)
project(sdat2cpp LANGUAGES CXX)

# C++17
set(CMAKE_CXX_STANDARD 17)

# Look for cwd please.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

# Detect the architecture
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x64")
else()
    set(ARCH "x86")
endif()

# Detect the build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(BUILD_TYPE "debug")
else()
    set(BUILD_TYPE "release")
endif()
set(EXECUTABLE_NAME "sdat2img-cpp-${ARCH}-${BUILD_TYPE}")

# Add the target
add_executable(${EXECUTABLE_NAME} sdat2img.cpp)

# Find brotli for inline-decompression
find_package(Brotli)
if (Brotli_FOUND)
    message(STATUS "Brotli found")
    add_compile_definitions(${EXECUTABLE_NAME} -DHAVE_BROTLI)
    target_link_libraries(${EXECUTABLE_NAME} Brotli::decoder)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)

if(LTO_SUPPORTED)
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET ${EXECUTABLE_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "IPO / LTO not supported: <${LTO_ERROR}>")
endif()