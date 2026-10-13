/**
 * @file    main.cpp
 * @brief   Entry point of the siggen GUI: initializes, runs and shuts down the application.
 */

#include "application.h"
#include "error_codes.h"
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

/**
 * @brief   Opens the desktop interface and runs it until the user closes the window.
 * @return  Core::ExitSuccess when the window closes normally; Core::ExitFailure when the window or the graphics
 *          context cannot be created, which Shutdown() reports on stderr and in a message box.
 * @note    The executable takes no arguments: the generator starts with default settings and presets load from the
 *          Signal Setup window. The log level comes from SPDLOG_LEVEL; logs go to stderr.
 */
int main(int, char **) {
    spdlog::set_default_logger(spdlog::stderr_color_mt("siggen"));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    spdlog::cfg::load_env_levels();
    spdlog::flush_on(spdlog::level::warn);

    GUI::Application application;
    spdlog::info("Starting Siggen GUI");
    int result = application.Init();
    if (result == Core::ExitSuccess) {
        result = application.Run();
    }
    application.Shutdown();
    return result;
}
