# ============================================================================
# NoteTake - Pure OpenGL Text Editor
# ============================================================================
cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(NoteTake VERSION 1.0.0 LANGUAGES CXX)

set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

# ============================================================================
# Source Files
# ============================================================================
file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Package Dependencies
# ============================================================================
find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

# ============================================================================
# Executable Target
# ============================================================================
add_executable(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/main.cpp ${USER_SOURCES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    fmt::fmt
    glfw
    glad::glad
    Freetype::Freetype
)


# ============================================================================
# Build Instructions for Developers
# ============================================================================
# To build this project:
# 1. Make sure vcpkg is installed and VCPKG_ROOT environment variable is set
# 2. Install dependencies: vcpkg install fmt glfw3 glad
# 3. Configure: cmake -S . -B build -G "Visual Studio 17 2022" -A x64
# 4. Build: cmake --build build --config Debug
# 5. Run: ./build/Debug/NoteTake.exe (on Windows)
# ============================================================================