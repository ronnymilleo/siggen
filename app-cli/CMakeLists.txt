# =============================================================================
# Application Command Line
# =============================================================================

add_library(siggen-cli-lib
        STATIC
            cli_config.cpp
            command_line.cpp
)

target_include_directories(siggen-cli-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(siggen-cli-lib
        PUBLIC
            siggen-core-lib
            CLI11::CLI11
)

siggen_target_options(siggen-cli-lib)
