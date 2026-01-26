cmake_minimum_required(VERSION 3.22)

project(CyberpunkVR VERSION 0.0.1 LANGUAGES CXX C)

# Standard Settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Preprocessor Definitions
add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DXR_USE_PLATFORM_WIN32 -DXR_USE_GRAPHICS_API_D3D12)

# Dependencies (Add them as subdirectories)
# This compels CMake to build them
add_subdirectory(deps/RED4ext.SDK)

# OpenXR SDK configuration
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE) # Build static loader
add_subdirectory(deps/OpenXR-SDK)

# Output Directory (plugins folder)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Source Files
file(GLOB_RECURSE SOURCES 
    "src/*.cpp" 
    "include/*.hpp"
)

# Create DLL
add_library(${PROJECT_NAME} SHARED ${SOURCES})

# Include Directories
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/RED4ext.SDK/include
    ${CMAKE_SOURCE_DIR}/deps/OpenXR-SDK/include
)

# Link Libraries
# We link against the targets defined by the subdirectories
target_link_libraries(${PROJECT_NAME} PRIVATE
    RED4ext.SDK
    openxr_loader
    d3d12
    dxgi
    psapi
)
