# ============================================================================
# OpenGL Project with GLFW, GLAD, fmt, and ImGui - CMake Configuration
# ============================================================================
# This is the root CMakeLists.txt file for an OpenGL project using modern C++23
# Dependencies are managed through vcpkg package manager
# ImGui backends are automatically fetched from GitHub via FetchContent
# ============================================================================

# Specify minimum CMake version
cmake_minimum_required(VERSION 3.24)

# ============================================================================
# vcpkg Integration Setup
# ============================================================================
# Set vcpkg toolchain file if not already defined
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

# ============================================================================
# Project Configuration
# ============================================================================
project(NoteTake VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# C++ Standard and Compiler Settings
# ============================================================================
set(CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)

# ============================================================================
# Fetch ImGui Backends
# ============================================================================
# Automatically download ImGui backends from GitHub at configure time
# This ensures version compatibility and eliminates manual file management
include(FetchContent)

FetchContent_Declare(
    imgui_backends
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9.zip
    SOURCE_SUBDIR backends
    DOWNLOAD_NO_PROGRESS FALSE
)

# Suppress FetchContent output
set(FETCHCONTENT_QUIET FALSE)
FetchContent_MakeAvailable(imgui_backends)

# ============================================================================
# Source Files Configuration
# ============================================================================
# Automatically find all .cpp files in your src/ directory
file(GLOB_RECURSE USER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
)

# Add ImGui backend implementations that were fetched
set(IMGUI_BACKEND_SOURCES
    "${imgui_backends_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_backends_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

# Combine all sources
set(ALL_SOURCES ${USER_SOURCES} ${IMGUI_BACKEND_SOURCES})

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories($ENV{VCPKG_ROOT}/installed/x64-windows/include)
include_directories(${imgui_backends_SOURCE_DIR})
include_directories(${imgui_backends_SOURCE_DIR}/backends)

# ============================================================================
# Package Dependencies
# ============================================================================
find_package(fmt CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# ============================================================================
# Executable Target Configuration
# ============================================================================
add_executable(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/main.cpp ${ALL_SOURCES})

# ============================================================================
# Library Linking Configuration
# ============================================================================
target_link_libraries(${PROJECT_NAME}
    PRIVATE 
    fmt::fmt        # Modern C++ formatting library
    glfw           # Window creation and input handling
    glad::glad     # OpenGL function loader
    imgui::imgui   # Immediate mode GUI library
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