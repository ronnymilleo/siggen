#include "cli_config.h"
#include "command_line.h"
#include "generator.h"
#include "iq_export.h"
#include <spdlog/cfg/env.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#if SIGGEN_HAS_GUI
int run_gui(const iq::GenerationConfig& config);
#endif

int main(int argc, char** argv)
{
    try
    {
        CommandLine command_line(SIGGEN_VERSION);
        try
        {
            command_line.app.parse(argc, argv);
        }
        catch (const CLI::ParseError& e)
        {
            return command_line.app.exit(e);
        }

        spdlog::set_default_logger(spdlog::stderr_color_mt("siggen"));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
        spdlog::cfg::load_env_levels();
        if (!command_line.log_level.empty())
            spdlog::set_level(spdlog::level::from_str(command_line.log_level));
        spdlog::flush_on(spdlog::level::warn);

        if (command_line.batch_selected())
        {
            const auto request = resolve_batch(command_line);
            spdlog::info("Generating batch into {}", request.output_dir.string());
            const auto summary = iq::run_batch(request);
            spdlog::info("Batch complete: {} frames across {} sweep points", summary.frame_count, summary.point_count);
            return 0;
        }

        const auto config = resolve_config(command_line);

        if (command_line.gui)
        {
#if SIGGEN_HAS_GUI
            spdlog::info("Starting Siggen GUI");
            return run_gui(config);
#else
            spdlog::error("GUI support is not included in this build. Build with the dev or release preset to use --gui.");
            return 1;
#endif
        }

        const auto format = resolve_format(command_line.format);
        const auto output = resolve_output(command_line);
        spdlog::debug("Generating {} signal", iq::modulation_name(config.modulation));
        const auto signal = iq::generate(config);
        iq::export_signal(output, signal, format, command_line.overwrite);
        spdlog::info("Exported {} complex samples to {} and its JSON metadata sidecar",
                     signal.samples.size(), output);
        return 0;
    }
    catch (const std::exception& e)
    {
        spdlog::error("Operation failed: {}", e.what());
        return 1;
    }
    catch (...)
    {
        spdlog::critical("Unknown error occurred");
        return 1;
    }
}
