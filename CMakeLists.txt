# =============================================================================
# Siggen: complex baseband signal generator
# =============================================================================
#
# Author: Ronny Milleo
# License: See LICENSE file in project root
# =============================================================================

cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are unsupported. Use cmake -S . -B build -G Ninja.")
endif()

project(siggen
    VERSION 0.5.0
    LANGUAGES CXX
)

# =============================================================================
# Overall project configs
# =============================================================================

option(SIGGEN_BUILD_GUI "Build the ImGui desktop application" ON)
option(SIGGEN_BUILD_CLI "Build the command-line tool" ON)
option(SIGGEN_BUILD_TESTS "Build the tests" OFF)
option(SIGGEN_ENABLE_WARNINGS "Enable project compiler warnings" ON)
option(SIGGEN_BUILD_PYTHON "Build the shared library used by the Python package" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executables and libraries go to out/<build-type>/ of the source tree, like imcsim, independent of the
# build directory that holds the CMake cache and the object files. Build trees that share a build type
# also share the output folder.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" SIGGEN_BUILD_TYPE_LOWER)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${SIGGEN_BUILD_TYPE_LOWER}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${SIGGEN_BUILD_TYPE_LOWER}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/out/${SIGGEN_BUILD_TYPE_LOWER}/lib")

# C++23, sanitizers and warnings, applied per target through siggen_target_options()
include(cmake/ProjectOptions.cmake)

# =============================================================================
# Formatting Target (clang-format)
# =============================================================================

find_program(CLANG_FORMAT NAMES clang-format)
if(CLANG_FORMAT)
    # Collect all source files for formatting
    file(GLOB_RECURSE ALL_SOURCE_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/app-core/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app-core/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/app-cli/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app-cli/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/app-gui/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/app-gui/*.h
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h
    )

    add_custom_target(format
            COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCE_FILES} main.cpp
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Formatting source code with clang-format"
            VERBATIM
    )
else()
    message(STATUS "clang-format not found; skipping automatic source formatting")
    add_custom_target(format
            COMMAND ${CMAKE_COMMAND} -E echo "clang-format not found, skipping formatting"
    )
endif()

# =============================================================================
# Dependencies and targets
# =============================================================================

function(siggen_require_submodule name file)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/vendor/${name}/${file}")
        message(FATAL_ERROR "Missing required ${name} submodule. Run: git submodule update --init --recursive")
    endif()
endfunction()

siggen_require_submodule(stb stb_image_write.h)
siggen_require_submodule(json include/nlohmann/json.hpp)
if(SIGGEN_BUILD_GUI OR SIGGEN_BUILD_CLI OR SIGGEN_BUILD_TESTS)
    find_package(Threads REQUIRED)
endif()
if(SIGGEN_BUILD_GUI OR SIGGEN_BUILD_CLI)
    siggen_require_submodule(spdlog CMakeLists.txt)
    siggen_require_submodule(cli11 CMakeLists.txt)
endif()
if(SIGGEN_BUILD_GUI)
    siggen_require_submodule(glfw CMakeLists.txt)
    siggen_require_submodule(imgui imgui.cpp)
    siggen_require_submodule(implot implot.cpp)
    siggen_require_submodule(stb stb_image.h)
endif()

if(SIGGEN_BUILD_GUI OR SIGGEN_BUILD_CLI)
    add_subdirectory(vendor)
endif()
add_subdirectory(app-core)
if(SIGGEN_BUILD_CLI)
    add_subdirectory(app-cli)
endif()
if(SIGGEN_BUILD_GUI)
    add_subdirectory(app-gui)
endif()

if(SIGGEN_BUILD_CLI)
    add_executable(siggen-cli app-cli/main.cpp)
    target_link_libraries(siggen-cli PRIVATE siggen-cli-lib spdlog::spdlog)
    target_compile_definitions(siggen-cli PRIVATE SIGGEN_VERSION="${PROJECT_VERSION}")
    siggen_target_options(siggen-cli)
    add_dependencies(siggen-cli format)
endif()

if(SIGGEN_BUILD_GUI)
    add_executable(siggen main.cpp)
    target_link_libraries(siggen PRIVATE siggen-gui-lib spdlog::spdlog)
    siggen_target_options(siggen)
    add_dependencies(siggen format)
endif()

# =============================================================================
# Testing
# =============================================================================

if(SIGGEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# Python package
# =============================================================================

# Shared C interface loaded by the `siggen` Python package with ctypes
if(SIGGEN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# =============================================================================
# Post-Build Commands
# =============================================================================

if(SIGGEN_BUILD_GUI AND EXISTS "${CMAKE_SOURCE_DIR}/assets")
    add_custom_command(TARGET siggen POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:siggen>/assets"
            COMMENT "Copying assets next to the executable"
            VERBATIM
    )
endif()

add_custom_target(copy_compile_commands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to project root"
        VERBATIM
)

if(SIGGEN_BUILD_GUI)
    add_dependencies(siggen copy_compile_commands)
elseif(SIGGEN_BUILD_CLI)
    add_dependencies(siggen-cli copy_compile_commands)
endif()

# =============================================================================
# Project Summary
# =============================================================================

message(STATUS "")
message(STATUS "==================== Summary ====================")
message(STATUS "Project:      ${PROJECT_NAME}")
message(STATUS "Version:      ${PROJECT_VERSION}")
message(STATUS "Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler:     ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Executables:  GUI=${SIGGEN_BUILD_GUI} CLI=${SIGGEN_BUILD_CLI}")
message(STATUS "Tests:        ${SIGGEN_BUILD_TESTS}")
message(STATUS "Python:       ${SIGGEN_BUILD_PYTHON}")
message(STATUS "Sanitizer:    ${SIGGEN_SANITIZER}")
message(STATUS "=================================================")
message(STATUS "")
