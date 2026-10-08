# Shared C interface used by the `siggen` Python package (ctypes; no extra dependencies).
set_target_properties(siggen-core-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(siggen_c SHARED csrc/siggen_c.cpp)
target_include_directories(siggen_c PRIVATE csrc)
target_link_libraries(siggen_c PRIVATE siggen-core-lib)
target_compile_definitions(siggen_c PRIVATE SIGGEN_VERSION="${PROJECT_VERSION}")
siggen_target_options(siggen_c)
set_target_properties(siggen_c PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

if(SIGGEN_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import numpy" RESULT_VARIABLE siggen_numpy_missing OUTPUT_QUIET ERROR_QUIET)
        if(siggen_numpy_missing)
            message(WARNING "NumPy not found for ${Python3_EXECUTABLE}; the Python package tests are not registered.")
        else()
            add_test(NAME Python.Package COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tests -v
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(Python.Package PROPERTIES ENVIRONMENT
                "SIGGEN_LIBRARY=$<TARGET_FILE:siggen_c>;PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR};SIGGEN_CLI=$<TARGET_FILE:siggen>")
        endif()
    endif()
endif()
