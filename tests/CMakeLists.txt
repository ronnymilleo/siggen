# =============================================================================
# Tests
# =============================================================================

set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock")
set(INSTALL_GTEST OFF CACHE BOOL "Install GoogleTest")
siggen_require_submodule(googletest CMakeLists.txt)
add_subdirectory(${PROJECT_SOURCE_DIR}/vendor/googletest ${CMAKE_BINARY_DIR}/vendor/googletest EXCLUDE_FROM_ALL)

include(GoogleTest)

# =============================================================================
# Core tests (no graphics, no command line)
# =============================================================================

add_executable(siggen-tests
            batch_test.cpp
            cross_offset_modulation_test.cpp
            export_test.cpp
            generation_job_test.cpp
            generator_test.cpp
            impairments_test.cpp
            measurements_test.cpp
            modulation_families_test.cpp
            noise_test.cpp
            pipeline_test.cpp
            plot_export_test.cpp
            preset_test.cpp
            signal_analysis_test.cpp
            signal_processing_test.cpp
            waveform_test.cpp
)

# stb_image decodes the PNG files the plot export tests write
target_include_directories(siggen-tests
        SYSTEM PRIVATE
            ${PROJECT_SOURCE_DIR}/vendor/stb
)

target_compile_definitions(siggen-tests
        PRIVATE
            SIGGEN_PRESET_DIR="${PROJECT_SOURCE_DIR}/presets"
)

target_link_libraries(siggen-tests
        PRIVATE
            siggen-core-lib
            GTest::gtest_main
            Threads::Threads
)

siggen_target_options(siggen-tests)
gtest_discover_tests(siggen-tests DISCOVERY_MODE PRE_TEST)

# =============================================================================
# Command line tests
# =============================================================================

if(SIGGEN_BUILD_APP)
    add_executable(siggen-cli-tests
                cli_config_test.cpp
                command_line_test.cpp
    )

    target_link_libraries(siggen-cli-tests
            PRIVATE
                siggen-cli-lib
                GTest::gtest_main
    )

    siggen_target_options(siggen-cli-tests)
    gtest_discover_tests(siggen-cli-tests DISCOVERY_MODE PRE_TEST)

    # Runs the real executable end to end, without a display
    add_test(NAME CLI.EndToEnd
            COMMAND ${CMAKE_COMMAND}
                -DEXECUTABLE=$<TARGET_FILE:siggen>
                -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-integration
                -DHAS_GUI=${SIGGEN_BUILD_GUI}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_integration.cmake
    )
endif()

# =============================================================================
# GUI tests (need a display for the rendering checks)
# =============================================================================

if(SIGGEN_BUILD_GUI)
    add_executable(siggen-gui-tests
                ui_smoke_test.cpp
    )

    target_include_directories(siggen-gui-tests
            SYSTEM PRIVATE
                ${PROJECT_SOURCE_DIR}/vendor/stb
    )

    target_link_libraries(siggen-gui-tests
            PRIVATE
                siggen-gui-lib
                GTest::gtest_main
    )

    siggen_target_options(siggen-gui-tests)
    gtest_discover_tests(siggen-gui-tests DISCOVERY_MODE PRE_TEST)
endif()
