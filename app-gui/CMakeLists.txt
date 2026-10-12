# =============================================================================
# Application GUI
# =============================================================================

add_library(siggen-gui-lib
        STATIC
            application.cpp
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
