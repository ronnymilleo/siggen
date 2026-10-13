# =============================================================================
# Application GUI
# =============================================================================

add_library(siggen-gui-lib
        STATIC
            application.cpp
            examples.cpp
            generator_session.cpp
            theme.cpp
            widgets.cpp
            window_manager.cpp
            windows/app_window.cpp
            windows/controls_window.cpp
            windows/summary_window.cpp
            windows/views_window.cpp
)

target_include_directories(siggen-gui-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# =============================================================================
# Bundled font
# =============================================================================

# The font is compiled in as a byte array, so text looks the same on every system
function(read_font_bytes font_file name)
    file(READ ${font_file} font_hex HEX)
    string(LENGTH "${font_hex}" hex_length)
    math(EXPR font_size "${hex_length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," font_bytes "${font_hex}")
    set(${name}_BYTES "${font_bytes}" PARENT_SCOPE)
    set(${name}_SIZE ${font_size} PARENT_SCOPE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${font_file})
endfunction()
read_font_bytes(${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf INTER_FONT)
configure_file(font_files.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/font_files.h @ONLY)

# =============================================================================
# Guided lesson presets
# =============================================================================

# Each example is compiled in as a string, so the binary needs no data files next to it. Editing or adding an
# example reruns CMake on the next build
file(GLOB EXAMPLE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.preset)
set(EXAMPLE_FILE_ENTRIES "")
foreach(example_file IN LISTS EXAMPLE_FILES)
    get_filename_component(example_name ${example_file} NAME)
    file(READ ${example_file} example_text)
    string(APPEND EXAMPLE_FILE_ENTRIES "    {\"${example_name}\", R\"siggen(${example_text})siggen\"},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${example_file})
endforeach()
configure_file(example_files.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/example_files.h @ONLY)

# stb_image loads the window icon
target_include_directories(siggen-gui-lib
        SYSTEM PRIVATE
            ${PROJECT_SOURCE_DIR}/vendor/stb
)

target_link_libraries(siggen-gui-lib
        PUBLIC
            siggen-core-lib
            imgui
            implot
            Threads::Threads
        PRIVATE
            spdlog::spdlog
)

siggen_target_options(siggen-gui-lib)
