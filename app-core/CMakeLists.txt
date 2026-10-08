# =============================================================================
# Application Core
# =============================================================================

add_library(siggen-core-lib
        STATIC
            analysis.cpp
            batch.cpp
            ber.cpp
            ber_job.cpp
            demodulator.cpp
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
            recording.cpp
            signal_analysis.cpp
            signal_processing.cpp
            theory.cpp
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

# nlohmann/json reads SigMF metadata; header-only, and the tests parse JSON with it too
target_include_directories(siggen-core-lib
        SYSTEM PUBLIC
            ${PROJECT_SOURCE_DIR}/vendor/json/include
)

siggen_target_options(siggen-core-lib)
