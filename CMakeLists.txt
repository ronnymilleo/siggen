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

# The numeric parsing uses std::from_chars on doubles, which the C++ library of macOS has from version 26. Only
# read on macOS, and set before project() so the compiler checks are made for it
set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0 CACHE STRING "Oldest macOS the program runs on")

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
# The C++ runtime is linked statically, so the Windows zip of a release runs without the Visual C++ redistributable
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# Executables and libraries go to out/<build-type>/ of the source tree, like imcsim, independent of the
# build directory that holds the CMake cache and the object files. A tree whose folder is not the documented
# plain "build" writes to out/<build-type>-<tree>/ instead, so trees that share a build type (sanitizers,
# headless) do not overwrite each other's archives and binaries.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()
string(TOLOWER "${CMAKE_BUILD_TYPE}" SIGGEN_BUILD_TYPE_LOWER)
get_filename_component(SIGGEN_BUILD_TREE "${CMAKE_BINARY_DIR}" NAME)
set(SIGGEN_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/${SIGGEN_BUILD_TYPE_LOWER}")
if(NOT SIGGEN_BUILD_TREE STREQUAL "build")
    string(REGEX REPLACE "^(cmake-)?build-?" "" SIGGEN_TREE_SUFFIX "${SIGGEN_BUILD_TREE}")
    set(SIGGEN_OUT_DIR "${SIGGEN_OUT_DIR}-${SIGGEN_TREE_SUFFIX}")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${SIGGEN_OUT_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${SIGGEN_OUT_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${SIGGEN_OUT_DIR}/lib")

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
    siggen_require_submodule(SDL CMakeLists.txt)
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
    if(WIN32)
        # The icon and the file properties Explorer shows; packaging/windows/make_icon.py packs the icon from
        # the PNGs in assets/
        enable_language(RC)
        target_sources(siggen PRIVATE
                packaging/windows/siggen.rc
                packaging/windows/siggen.manifest)
        # The version of the file properties, which siggen.rc cannot split out of project() itself
        target_compile_definitions(siggen PRIVATE
                $<$<COMPILE_LANGUAGE:RC>:SIGGEN_VERSION="${PROJECT_VERSION}">
                $<$<COMPILE_LANGUAGE:RC>:SIGGEN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}>
                $<$<COMPILE_LANGUAGE:RC>:SIGGEN_VERSION_MINOR=${PROJECT_VERSION_MINOR}>
                $<$<COMPILE_LANGUAGE:RC>:SIGGEN_VERSION_PATCH=${PROJECT_VERSION_PATCH}>)
        # Release builds open no console window behind the program, and keep main() as the entry point; Debug
        # builds keep the console for the logs of SDL and the GPU validation layers
        if(MSVC)
            target_link_options(siggen PRIVATE $<$<NOT:$<CONFIG:Debug>>:/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup>)
        endif()
    elseif(APPLE)
        # An application bundle, siggen.app, with the icon packaging/macos/make_icns.py writes from assets/
        set_source_files_properties(packaging/macos/siggen.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(siggen PRIVATE packaging/macos/siggen.icns)
        set_target_properties(siggen PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
                MACOSX_BUNDLE_GUI_IDENTIFIER io.github.ronnymilleo.siggen
                MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
    endif()
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
# Installation
# =============================================================================

# Windows gets a folder to unzip anywhere, macOS the application bundle, and Linux the standard layout, with the
# desktop entry and the icon. All carry the licenses of what the program includes. The window icon, assets/icon.png,
# travels with the program: the GUI looks for it next to the executable and in ../share/siggen/assets
if(WIN32)
    set(SIGGEN_DOC_DIR .)
    if(SIGGEN_BUILD_GUI)
        install(TARGETS siggen RUNTIME DESTINATION .)
        install(FILES assets/icon.png DESTINATION assets)
    endif()
    if(SIGGEN_BUILD_CLI)
        install(TARGETS siggen-cli RUNTIME DESTINATION .)
    endif()
elseif(APPLE)
    set(SIGGEN_DOC_DIR siggen.app/Contents/Resources)
    if(SIGGEN_BUILD_GUI)
        install(TARGETS siggen BUNDLE DESTINATION .)
    endif()
else()
    include(GNUInstallDirs)
    set(SIGGEN_DOC_DIR ${CMAKE_INSTALL_DOCDIR})
    if(SIGGEN_BUILD_GUI)
        install(TARGETS siggen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES assets/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/siggen/assets)
        install(FILES packaging/siggen.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
        install(FILES assets/icon.png RENAME siggen.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    endif()
    if(SIGGEN_BUILD_CLI)
        install(TARGETS siggen-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
install(FILES LICENSE README.md THIRD_PARTY_NOTICES.md DESTINATION ${SIGGEN_DOC_DIR})
install(FILES vendor/json/LICENSE.MIT RENAME json-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
install(FILES vendor/stb/LICENSE RENAME stb-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
if(SIGGEN_BUILD_GUI)
    install(FILES vendor/imgui/LICENSE.txt RENAME imgui-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
    install(FILES vendor/implot/LICENSE RENAME implot-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
    install(FILES vendor/SDL/LICENSE.txt RENAME SDL-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
    install(FILES assets/fonts/Inter-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
endif()
if(SIGGEN_BUILD_GUI OR SIGGEN_BUILD_CLI)
    install(FILES vendor/spdlog/LICENSE RENAME spdlog-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
    install(FILES vendor/cli11/LICENSE RENAME cli11-LICENSE.txt DESTINATION ${SIGGEN_DOC_DIR}/licenses)
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
