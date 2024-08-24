cmake_minimum_required(VERSION 3.28)
project(sdat2cpp LANGUAGES CXX)

# Look for cwd please.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

# Add the target
add_executable(sdat2cpp sdat2img.cpp)

# Find brotli for inline-decompression
find_package(Brotli)
if (Brotli_FOUND)
    message(STATUS "Brotli found")
    add_compile_definitions(${PROJECT_NAME} -DHAVE_BROTLI)
    target_link_libraries(${PROJECT_NAME} Brotli::decoder)
endif()