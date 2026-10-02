#include "signal_generator.h"
#include "imgui.h"
#include "implot.h"
#include "preset.h"
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

SignalGenerator::SignalGenerator(iq::GenerationConfig config)
    : ImGuiWindowLayer("Signal Generator"), config_(std::move(config)), bit_input_(iq::MAX_EXPLICIT_BITS + 1, 0)
{
    const auto count = std::min(config_.bits.size(), bit_input_.size() - 1);
    std::copy_n(config_.bits.begin(), count, bit_input_.begin());
}
void SignalGenerator::DrawContents()
{
    try
    {
        if (job_.poll())
        {
            const auto& r = *job_.result();
            if (r.family == iq::Family::Noise)
                spdlog::info("Generated {}: {} complex samples at {} Hz", iq::modulation_name(r.config.modulation),
                             r.samples.size(), r.sample_rate_hz);
            else
                spdlog::info("Generated {}: {} symbols, {} complex samples at {} Hz",
                             iq::modulation_name(r.config.modulation), r.symbols.size(), r.samples.size(),
                             r.sample_rate_hz);
            plots_    = {};
            spectrum_ = {};
            spectrum_db_.clear();
            waveform_fit_ = true;
            spectrum_fit_ = true;
            plots_        = make_plot_data(r);
            spectrum_     = iq::welch_psd(r.samples, r.sample_rate_hz);
            spectrum_db_.resize(spectrum_.power_density.size());
            std::transform(spectrum_.power_density.begin(), spectrum_.power_density.end(), spectrum_db_.begin(),
                           [](double p) { return 10 * std::log10(std::max(p, 1e-20)); });
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error("Generation or analysis failed: {}", e.what());
        error_ = e.what();
    }
    ImGui::SeparatorText("Signal Setup");
    if (ImGui::BeginCombo("Modulation", iq::modulation_name(config_.modulation))) {
        for (const auto& waveform : iq::waveforms()) {
            const bool selected = waveform.modulation == config_.modulation;
            if (ImGui::Selectable(waveform.canonical_name.data(), selected)) {
                iq::select_waveform(config_, waveform.modulation);
                std::fill(bit_input_.begin(), bit_input_.end(), 0);
                std::copy(config_.bits.begin(), config_.bits.end(), bit_input_.begin());
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    const bool noise_source = !iq::waveform_descriptor(config_.modulation).shaped;
    if (noise_source)
    {
        ImGui::InputInt("Sample count", &config_.noise_source.sample_count);
        ImGui::InputDouble("Sample rate (Hz)", &config_.noise_source.sample_rate_hz, 100, 1000, "%.6g");
        ImGui::InputDouble("Noise power", &config_.noise_source.noise_power, .1, 1, "%.6g");
        ImGui::TextWrapped("Complex WGN: independent Gaussian I/Q components, each with variance power/2 before gain.");
        ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config_.noise_seed);
        ImGui::InputDouble("Amplitude gain", &config_.amplitude_gain, .1, 1, "%.6g");
    }
    else
    {
        ImGui::InputInt("Symbol count", &config_.symbol_count);
        ImGui::InputDouble("Symbol rate (Bd)", &config_.symbol_rate_baud, 100, 1000, "%.6g");
        ImGui::InputInt("Samples per symbol", &config_.samples_per_symbol);
        ImGui::Text("Sample rate: %.6g Hz", config_.symbol_rate_baud * config_.samples_per_symbol);
        ImGui::InputDouble("Amplitude gain", &config_.amplitude_gain, .1, 1, "%.6g");
        ImGui::SeparatorText("Pulse Shaping");
        int pulse = static_cast<int>(config_.pulse);
        if (ImGui::Combo("Pulse", &pulse, "Root-raised cosine\0Rectangular\0"))
            config_.pulse = static_cast<iq::Pulse>(pulse);
        ImGui::BeginDisabled(config_.pulse != iq::Pulse::RRC);
        ImGui::InputDouble("RRC roll-off", &config_.roll_off, .05, .1, "%.4g");
        ImGui::InputInt("RRC span (symbols)", &config_.span_symbols);
        ImGui::EndDisabled();
        ImGui::SeparatorText("AWGN");
        ImGui::Checkbox("Add AWGN", &config_.awgn.enabled);
        ImGui::BeginDisabled(!config_.awgn.enabled);
        ImGui::InputDouble("SNR (dB)", &config_.awgn.snr_db, 1, 10, "%.6g");
        ImGui::EndDisabled();
        ImGui::TextWrapped("SNR is clean sample power over added complex noise power, measured on the steady-state interval.");
        ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config_.noise_seed);
        ImGui::InputScalar("Random seed", ImGuiDataType_U32, &config_.seed);
        if (ImGui::CollapsingHeader("Advanced"))
        {
            int source = static_cast<int>(config_.data_source);
            if (ImGui::Combo("Data source", &source, "Seeded random bits\0Explicit bits\0"))
                config_.data_source = static_cast<iq::DataSource>(source);
            if (config_.data_source == iq::DataSource::Explicit)
            {
                if (ImGui::InputTextMultiline("Bits", bit_input_.data(), bit_input_.size(), ImVec2(-1, 70)))
                    config_.bits = bit_input_.data();
                ImGui::TextUnformatted("Enter exactly symbol count x bits per symbol digits (0 or 1).");
            }
        }
    }
    std::string validation;
    try
    {
        iq::validate(config_);
    }
    catch (const std::exception& e)
    {
        validation = e.what();
    }
    if (!validation.empty())
        ImGui::TextColored(ImVec4(.96f, .45f, .40f, 1.f), "Invalid settings: %s", validation.c_str());
    ImGui::Separator();
    ImGui::BeginDisabled(job_.busy() || !validation.empty());
    if (ImGui::Button("Generate Signal"))
    {
        try
        {
            if (job_.start(config_))
                spdlog::debug("Started {} generation: {} symbols", iq::modulation_name(config_.modulation),
                              config_.symbol_count);
            error_.clear();
        }
        catch (const std::exception& e)
        {
            spdlog::error("Unable to start generation: {}", e.what());
            error_ = e.what();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!job_.result());
    if (ImGui::Button("Export I/Q..."))
    {
        export_result_ = job_.result();
        export_status_.clear();
        confirm_overwrite_ = false;
        ImGui::OpenPopup("Export I/Q");
    }
    ImGui::EndDisabled();
    DrawExportDialog();
    if (job_.busy())
    {
        ImGui::SameLine();
        ImGui::TextUnformatted("Generating...");
    }
    if (!error_.empty())
        ImGui::TextColored(ImVec4(.96f, .45f, .40f, 1.f), "Operation failed: %s", error_.c_str());
    if (ImGui::CollapsingHeader("Presets"))
    {
        ImGui::InputText("Preset path", preset_path_, sizeof preset_path_);
        if (ImGui::Button("Save Preset"))
        {
            try
            {
                iq::save_preset(preset_path_, config_);
                spdlog::info("Saved preset: {}", preset_path_);
                error_.clear();
            }
            catch (const std::exception& e)
            {
                spdlog::error("Preset save failed: {}", e.what());
                error_ = e.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Preset"))
        {
            try
            {
                auto loaded = iq::load_preset(preset_path_);
                config_     = std::move(loaded);
                spdlog::info("Loaded preset: {}", preset_path_);
                std::fill(bit_input_.begin(), bit_input_.end(), 0);
                std::copy(config_.bits.begin(), config_.bits.end(), bit_input_.begin());
                error_.clear();
            }
            catch (const std::exception& e)
            {
                spdlog::error("Preset load failed: {}", e.what());
                error_ = e.what();
            }
        }
    }
    ImGui::SeparatorText("Signal Summary");
    if (const auto& r = job_.result())
    {
        if (r->config != config_)
            ImGui::TextColored(ImVec4(.95f, .75f, .30f, 1.f), "Settings changed. Generate to update the displayed result.");
        if (r->family == iq::Family::Noise)
        {
            double total = 0;
            for (const auto& sample : r->samples) total += std::norm(sample);
            const auto mean_power = r->samples.empty() ? 0 : total / static_cast<double>(r->samples.size());
            ImGui::Text("%s | %zu complex samples", iq::modulation_name(r->config.modulation), r->samples.size());
            ImGui::Text("Sample rate: %.6g Hz | Duration: %.6g s", r->sample_rate_hz,
                        r->samples.size() / r->sample_rate_hz);
            ImGui::Text("Configured noise power: %.6g | Measured mean power: %.6g | Gain: %.6g",
                        r->config.noise_source.noise_power, mean_power, r->config.amplitude_gain);
            ImGui::Text("Noise seed: %u", r->noise.noise_seed);
        }
        else
        {
            ImGui::Text("%s | %zu symbols | %zu complex samples", iq::modulation_name(r->config.modulation),
                        r->symbols.size(), r->samples.size());
            ImGui::Text("Sample rate: %.6g Hz | Buffer duration: %.6g s", r->sample_rate_hz,
                        r->samples.size() / r->sample_rate_hz);
            ImGui::Text("Filter delay: %zu samples (%.6g s) | Gain: %.6g", r->filter_delay_samples,
                        r->filter_delay_samples / r->sample_rate_hz, r->config.amplitude_gain);
            if (r->noise.awgn_applied)
                ImGui::Text("AWGN: requested %.6g dB | reference power %.6g over [%zu,%zu) | added noise power %.6g | noise seed %u",
                            r->noise.requested_snr_db, r->noise.reference_power, r->noise.reference_begin,
                            r->noise.reference_end, r->noise.added_noise_power, r->noise.noise_seed);
        }
        DrawPlots();
    }
    else
        ImGui::TextDisabled("No signal generated yet. Choose settings and select Generate Signal.");
}

void SignalGenerator::DrawPlots()
{
    if (!ImGui::BeginTabBar("Signal views"))
        return;
    ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(.15f, .15f));
    if (ImGui::BeginTabItem("Waveform"))
    {
        ImGui::Text("I: blue | Q: orange | %zu plotted points (min/max reduction)", plots_.time.size());
        if (waveform_fit_)
            ImPlot::SetNextAxesToFit();
        if (ImPlot::BeginPlot("Complex baseband", ImVec2(-1, 280)))
        {
            waveform_fit_ = false;
            ImPlot::SetupAxes("Time (s)", "Amplitude");
            ImPlot::SetNextLineStyle(ImVec4(.2f, .6f, 1.f, 1.f));
            ImPlot::PlotLine("I", plots_.time.data(), plots_.i.data(), static_cast<int>(plots_.time.size()));
            ImPlot::SetNextLineStyle(ImVec4(1.f, .55f, .15f, 1.f));
            ImPlot::PlotLine("Q", plots_.time.data(), plots_.q.data(), static_cast<int>(plots_.time.size()));
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    const auto& result = job_.result();
    const bool noise_source = result && result->family == iq::Family::Noise;
    if (!noise_source && ImGui::BeginTabItem("Constellation"))
    {
        const bool noisy = result && result->noise.awgn_applied;
        ImGui::Combo("View", &constellation_view_,
                     noisy ? "Mapped symbols (ideal, gain applied)\0Matched filter (noisy observations)\0"
                           : "Mapped symbols (gain applied)\0Matched filter (steady-state symbols)\0");
        const auto& data = constellation_view_ == 0 ? plots_.mapped : plots_.matched;
        if (data.x.empty())
            ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
        if (ImPlot::BeginPlot("I/Q constellation", ImVec2(-1, 320), ImPlotFlags_Equal))
        {
            ImPlot::SetupAxes("In-phase (I)", "Quadrature (Q)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, ImVec4(.2f, .6f, 1.f, 1.f));
            ImPlot::PlotScatter("Symbols", data.x.data(), data.y.data(), static_cast<int>(data.x.size()));
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Spectrum"))
    {
        ImGui::Text("Two-sided Welch PSD | Periodic Hann | %zu samples/segment | %zu segments",
                    spectrum_.segment_length, spectrum_.segment_count);
        ImGui::TextWrapped("Relative power density; no impedance or watt/dBm calibration. Display floor: -200 dB.");
        if (spectrum_db_.empty())
            ImGui::TextUnformatted("At least four samples are needed for a spectrum.");
        else
        {
            if (spectrum_fit_)
                ImPlot::SetNextAxesToFit();
            if (ImPlot::BeginPlot("Baseband PSD", ImVec2(-1, 300)))
            {
                spectrum_fit_ = false;
                ImPlot::SetupAxes("Frequency (Hz)", "PSD (dB re 1 amplitude^2/Hz)");
                ImPlot::SetNextLineStyle(ImVec4(.2f, .6f, 1.f, 1.f));
                ImPlot::PlotLine("I + jQ", spectrum_.frequency_hz.data(), spectrum_db_.data(),
                                 static_cast<int>(spectrum_db_.size()));
                ImPlot::EndPlot();
            }
        }
        ImGui::EndTabItem();
    }
    ImPlot::PopStyleVar();
    ImGui::EndTabBar();
}

void SignalGenerator::Export(bool overwrite)
{
    try
    {
        iq::export_signal(export_path_, *export_result_, static_cast<iq::ExportFormat>(export_format_), overwrite);
        spdlog::info("Exported {} complex samples to {}", export_result_->samples.size(), export_path_);
        export_status_     = "Exported samples and JSON metadata.";
        confirm_overwrite_ = false;
    }
    catch (const std::exception& e)
    {
        spdlog::error("Export to {} failed: {}", export_path_, e.what());
        export_status_ = std::string("Export failed: ") + e.what();
    }
}
void SignalGenerator::DrawExportDialog()
{
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export I/Q", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("Export the completed signal captured when this dialog opened.");
    if (ImGui::InputText("Destination", export_path_, sizeof export_path_))
        confirm_overwrite_ = false;
    if (ImGui::Combo("Format", &export_format_, "CSV\0Binary float32 I/Q\0"))
        confirm_overwrite_ = false;
    ImGui::TextWrapped("Metadata is written beside the samples as <destination>.json.");
    if (ImGui::Button("Export"))
    {
        try
        {
            confirm_overwrite_ =
                std::filesystem::exists(export_path_) || std::filesystem::exists(iq::metadata_path(export_path_));
            if (!confirm_overwrite_)
                Export(false);
        }
        catch (const std::exception& e)
        {
            spdlog::error("Unable to inspect export destination {}: {}", export_path_, e.what());
            export_status_ = e.what();
        }
    }
    if (confirm_overwrite_)
    {
        ImGui::TextWrapped("The destination or metadata exists. Replace both files?");
        if (ImGui::Button("Confirm overwrite"))
            Export(true);
        ImGui::SameLine();
        if (ImGui::Button("Cancel overwrite"))
            confirm_overwrite_ = false;
    }
    if (!export_status_.empty())
        ImGui::TextWrapped("%s", export_status_.c_str());
    if (ImGui::Button("Close"))
    {
        export_result_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
