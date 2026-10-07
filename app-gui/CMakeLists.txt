# =============================================================================
# Application GUI
# =============================================================================

add_library(siggen-gui-lib
        STATIC
            application.cpp
            generator_session.cpp
            gui_runner.cpp
            imgui_layer.cpp
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
)

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
