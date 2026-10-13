# =============================================================================
# spdlog
# =============================================================================

set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build shared spdlog")
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build spdlog examples")
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "Build header-only spdlog examples")
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build spdlog tests")
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "Build header-only spdlog tests")
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Build spdlog benchmarks")
set(SPDLOG_INSTALL OFF CACHE BOOL "Install spdlog")
add_subdirectory(spdlog)

# =============================================================================
# SDL
# =============================================================================

# Built from the submodule, not taken from the system, and linked statically: every platform carries the same
# version inside the executable, with no library to ship next to it. Its GPU API renders Dear ImGui (Direct3D 12 on
# Windows, Vulkan on Linux), and its tests, examples and test library stay out
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)
# Subsystems the program never uses stay out, so the build does not depend on which of their libraries the machine
# has installed; video, GPU, dialogs and the gamepad (navigation in Dear ImGui) stay in
set(SDL_AUDIO OFF)
set(SDL_CAMERA OFF)
set(SDL_RENDER OFF)
set(SDL_HAPTIC OFF)
set(SDL_POWER OFF)
set(SDL_SENSOR OFF)
add_subdirectory(SDL)

# =============================================================================
# imgui
# =============================================================================

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
)

target_include_directories(imgui
    PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_link_libraries(imgui
    PUBLIC
        SDL3::SDL3
)

target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)

# =============================================================================
# implot
# =============================================================================

add_library(implot STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/implot/implot_items.cpp
)

target_include_directories(implot
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/implot
        ${CMAKE_CURRENT_SOURCE_DIR}/imgui
)

target_link_libraries(implot
    PUBLIC
        imgui
)
