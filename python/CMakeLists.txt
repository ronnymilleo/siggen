# Shared C interface used by the `siggen` Python package (ctypes; no extra dependencies).
set_target_properties(siggen-core-lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(siggen_c SHARED csrc/siggen_c.cpp)
target_include_directories(siggen_c PRIVATE csrc)
target_link_libraries(siggen_c PRIVATE siggen-core-lib)
target_compile_definitions(siggen_c PRIVATE SIGGEN_VERSION="${PROJECT_VERSION}")
siggen_target_options(siggen_c)
set_target_properties(siggen_c PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Install the library inside the `siggen` package, next to __init__.py, so the wheel built by scikit-build-core
# finds it there (_find_library() looks in the package directory first). The DLL is a RUNTIME artifact on Windows,
# the .so/.dylib a LIBRARY artifact on Linux/macOS.
install(TARGETS siggen_c
        RUNTIME DESTINATION siggen
        LIBRARY DESTINATION siggen
        ARCHIVE DESTINATION siggen)

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
                "SIGGEN_LIBRARY=$<TARGET_FILE:siggen_c>;PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endif()
endif()
