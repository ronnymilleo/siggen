add_library(signalgenerator STATIC signal_processing.cpp waveform.cpp noise.cpp impairments.cpp generator.cpp signal_analysis.cpp measurements.cpp preset.cpp iq_export.cpp batch.cpp)
target_include_directories(signalgenerator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
siggen_target_options(signalgenerator)
