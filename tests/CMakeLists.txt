add_executable(siggen_tests signal_processing_test.cpp waveform_test.cpp generator_test.cpp noise_test.cpp generation_job_test.cpp signal_analysis_test.cpp preset_test.cpp export_test.cpp batch_test.cpp measurements_test.cpp impairments_test.cpp)
target_link_libraries(siggen_tests PRIVATE signalgenerator GTest::gtest_main Threads::Threads)
siggen_target_options(siggen_tests)
include(GoogleTest)
gtest_discover_tests(siggen_tests DISCOVERY_MODE PRE_TEST)

target_include_directories(siggen_tests PRIVATE ${PROJECT_SOURCE_DIR}/application)
target_compile_definitions(siggen_tests PRIVATE SIGGEN_PRESET_DIR="${PROJECT_SOURCE_DIR}/presets")
if(SIGGEN_BUILD_APP)
    add_executable(siggen_cli_tests command_line_test.cpp cli_config_test.cpp ${PROJECT_SOURCE_DIR}/application/cli_config.cpp)
    target_include_directories(siggen_cli_tests PRIVATE ${PROJECT_SOURCE_DIR}/application)
    target_link_libraries(siggen_cli_tests PRIVATE signalgenerator CLI11::CLI11 GTest::gtest_main)
    siggen_target_options(siggen_cli_tests)
    gtest_discover_tests(siggen_cli_tests DISCOVERY_MODE PRE_TEST)
    add_test(NAME CLI.EndToEnd COMMAND ${CMAKE_COMMAND}
        -DEXECUTABLE=$<TARGET_FILE:siggen>
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-integration
        -DHAS_GUI=${SIGGEN_BUILD_GUI}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_integration.cmake)

endif()
if(SIGGEN_BUILD_GUI)
    add_executable(siggen_ui_tests ui_smoke_test.cpp)
    target_include_directories(siggen_ui_tests PRIVATE ${PROJECT_SOURCE_DIR}/application)
    target_include_directories(siggen_ui_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/vendor/stb)
    target_link_libraries(siggen_ui_tests PRIVATE siggen_ui GTest::gtest_main)
    siggen_target_options(siggen_ui_tests)
    gtest_discover_tests(siggen_ui_tests DISCOVERY_MODE PRE_TEST)
endif()
