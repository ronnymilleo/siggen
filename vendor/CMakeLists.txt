# =============================================================================
# Vendor libraries setup
# =============================================================================

cmake_minimum_required(VERSION 3.28)

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build shared spdlog")
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build spdlog examples")
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "Build header-only spdlog examples")
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build spdlog tests")
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "Build header-only spdlog tests")
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Build spdlog benchmarks")
set(SPDLOG_INSTALL OFF CACHE BOOL "Install spdlog")
add_subdirectory(spdlog)

set(CLI11_BUILD_DOCS OFF CACHE BOOL "Build CLI11 documentation")
set(CLI11_BUILD_TESTS OFF CACHE BOOL "Build CLI11 tests")
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "Build CLI11 examples")
set(CLI11_INSTALL OFF CACHE BOOL "Install CLI11")
add_subdirectory(cli11)

# CLI-only builds never discover or compile graphics dependencies.
if(NOT SIGGEN_BUILD_GUI)
    return()
endif()

find_package(OpenGL REQUIRED)

# Normal variable: keep GLFW static without changing the parent/cache policy.
set(BUILD_SHARED_LIBS OFF)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW documentation")
set(GLFW_INSTALL OFF CACHE BOOL "Install GLFW")
add_subdirectory(glfw)

add_library(imgui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/glfw/include
)

target_link_libraries(imgui
    PUBLIC
        glfw
        OpenGL::GL
)

add_library(implot STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/implot
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_link_libraries(implot
    PUBLIC
        imgui
)

# =============================================================================
# End of file
# =============================================================================
