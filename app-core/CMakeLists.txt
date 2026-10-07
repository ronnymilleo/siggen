# =============================================================================
# Application Core
# =============================================================================

add_library(siggen-core-lib
        STATIC
            batch.cpp
            generation_job.cpp
            generator.cpp
            impairments.cpp
            iq_export.cpp
            measurements.cpp
            noise.cpp
            pipeline.cpp
            plot_data.cpp
            plot_export.cpp
            plot_figures.cpp
            preset.cpp
            signal_analysis.cpp
            signal_processing.cpp
            waveform.cpp
)

target_include_directories(siggen-core-lib
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
)

# stb writes PNG images and rasterizes the embedded font of exported plots
target_include_directories(siggen-core-lib
        SYSTEM PRIVATE
            ${PROJECT_SOURCE_DIR}/vendor/stb
)

siggen_target_options(siggen-core-lib)
