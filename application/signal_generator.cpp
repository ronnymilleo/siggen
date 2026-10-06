#include "signal_generator.h"
#include "imgui.h"
#include "implot.h"
#include "preset.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <spdlog/spdlog.h>

namespace
{
void Metric(const char* label, const std::string& value, bool first = false)
{
    if (!first)
        ImGui::SameLine(0, 28);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    ImGui::TextUnformatted(value.c_str());
    ImGui::EndGroup();
}
// Plain-language help shown when the preceding control is hovered.
void Hint(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
std::string num(const char* f, double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}
} // namespace

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
            power_        = iq::power_statistics(r.samples);
            accuracy_     = iq::symbol_accuracy(r);
            eye_          = r.family == iq::Family::Noise ? iq::EyeDiagram{} : iq::eye_diagram(r);
            UpdateSpectrum();
        }
    }
    catch (const std::exception& e)
    {
        spdlog::error("Generation or analysis failed: {}", e.what());
        error_ = e.what();
    }
    const bool two_pane = ImGui::BeginTable("layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
                                                             ImGuiTableFlags_SizingStretchProp);
    if (two_pane)
    {
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch, .36f);
        ImGui::TableSetupColumn("results", ImGuiTableColumnFlags_WidthStretch, .64f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
    }
    ImGui::PushItemWidth(std::clamp(ImGui::GetContentRegionAvail().x * .5f, 150.f, 360.f));
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
    Hint("BPSK, QPSK, 8-PSK, 16-QAM and 64-QAM carry 1, 2, 3, 4 and 6 bits per symbol with Gray labelling. WGN is a pure noise source.");
    const bool noise_source = !iq::waveform_descriptor(config_.modulation).shaped;
    if (noise_source)
    {
        ImGui::InputInt("Sample count", &config_.noise_source.sample_count);
        Hint("Number of complex noise samples to generate.");
        ImGui::InputDouble("Sample rate (Hz)", &config_.noise_source.sample_rate_hz, 100, 1000, "%.6g");
        Hint("Sets the frequency axis of the spectrum: the PSD spans plus/minus half this rate.");
        ImGui::InputDouble("Noise power", &config_.noise_source.noise_power, .1, 1, "%.6g");
        Hint("Total complex noise power. Each of I and Q has half of it as variance.");
        ImGui::TextWrapped("Complex WGN: independent Gaussian I/Q components, each with variance power/2 before gain.");
        ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config_.noise_seed);
        ImGui::InputDouble("Amplitude gain", &config_.amplitude_gain, .1, 1, "%.6g");
        Hint("Scales every sample once. Power scales with gain squared.");
    }
    else
    {
        ImGui::InputInt("Symbol count", &config_.symbol_count);
        Hint("How many symbols to transmit. Each symbol carries log2(M) bits: 1 for BPSK, 2 for QPSK, 3 for 8-PSK, 4 for 16-QAM, 6 for 64-QAM.");
        ImGui::InputDouble("Symbol rate (Bd)", &config_.symbol_rate_baud, 100, 1000, "%.6g");
        Hint("Symbols per second. Together with samples per symbol it sets the sample rate.");
        ImGui::InputInt("Samples per symbol", &config_.samples_per_symbol);
        Hint("Oversampling factor (SPS). Higher values give smoother waveforms and more room in the spectrum, at the cost of more samples. RRC needs at least 2.");
        ImGui::Text("Sample rate: %.6g Hz", config_.symbol_rate_baud * config_.samples_per_symbol);
        ImGui::InputDouble("Amplitude gain", &config_.amplitude_gain, .1, 1, "%.6g");
        Hint("Scales every sample once. Power scales with gain squared.");
        ImGui::SeparatorText("Pulse Shaping");
        int pulse = static_cast<int>(config_.pulse);
        if (ImGui::Combo("Pulse", &pulse, "Root-raised cosine\0Rectangular\0"))
            config_.pulse = static_cast<iq::Pulse>(pulse);
        Hint("RRC is the practical choice: with a matching receive filter it has no inter-symbol interference at the decision instants and a compact spectrum. Rectangular pulses open the eye fully but have wide sinc sidelobes.");
        ImGui::BeginDisabled(config_.pulse != iq::Pulse::RRC);
        ImGui::InputDouble("RRC roll-off", &config_.roll_off, .05, .1, "%.4g");
        Hint("Excess bandwidth of the root-raised-cosine filter, from 0 to 1. Occupied bandwidth is about symbol rate x (1 + roll-off). Low values give narrow spectra but longer, more sensitive filter tails.");
        ImGui::InputInt("RRC span (symbols)", &config_.span_symbols);
        Hint("Filter length in symbols. Longer filters approximate the ideal response better (less residual ISI) and cost more samples.");
        ImGui::EndDisabled();
        ImGui::SeparatorText("AWGN");
        ImGui::Checkbox("Add AWGN", &config_.awgn.enabled);
        Hint("Additive white Gaussian noise at the SNR below. Disabled keeps the clean signal.");
        ImGui::BeginDisabled(!config_.awgn.enabled);
        ImGui::InputDouble("SNR (dB)", &config_.awgn.snr_db, 1, 10, "%.6g");
        Hint("Clean signal power divided by added noise power, in dB. Lower values spread the constellation and close the eye.");
        ImGui::EndDisabled();
        ImGui::TextWrapped("SNR is clean sample power over added complex noise power, measured on the steady-state interval.");
        ImGui::InputScalar("Noise seed", ImGuiDataType_U32, &config_.noise_seed);
        ImGui::InputScalar("Random seed", ImGuiDataType_U32, &config_.seed);
        Hint("Same settings and seeds always reproduce the same bits and noise. The data and noise seeds are independent streams.");
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
    ImGui::PopItemWidth();
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
        Hint("Guided lessons ship in the presets/ folder, for example presets/01-qpsk-clean.preset. Loading one shows its notes below.");
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
                preset_notes_ = iq::load_preset_notes(preset_path_);
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
    if (!preset_notes_.empty())
    {
        ImGui::SeparatorText("Preset notes");
        ImGui::TextWrapped("%s", preset_notes_.c_str());
    }
    if (two_pane)
        ImGui::TableNextColumn();
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
            Metric("Signal", std::string(iq::modulation_name(r->config.modulation)), true);
            Metric("Samples", std::to_string(r->samples.size()));
            Metric("Sample rate", num("%.6g Hz", r->sample_rate_hz));
            Metric("Duration", num("%.6g s", r->samples.size() / r->sample_rate_hz));
            Metric("Noise power (set)", num("%.6g", r->config.noise_source.noise_power), true);
            Metric("Mean power (measured)", num("%.6g", mean_power));
            Metric("Gain", num("%.6g", r->config.amplitude_gain));
            Metric("Noise seed", std::to_string(r->noise.noise_seed));
        }
        else
        {
            Metric("Signal", std::string(iq::modulation_name(r->config.modulation)), true);
            Metric("Symbols", std::to_string(r->symbols.size()));
            Metric("Samples", std::to_string(r->samples.size()));
            Metric("Sample rate", num("%.6g Hz", r->sample_rate_hz));
            Metric("Duration", num("%.6g s", r->samples.size() / r->sample_rate_hz));
            Metric("Filter delay", std::to_string(r->filter_delay_samples) + " samples", true);
            Metric("Gain", num("%.6g", r->config.amplitude_gain));
            if (r->noise.awgn_applied)
                ImGui::TextWrapped("AWGN: requested %.6g dB | reference power %.6g over [%zu,%zu) | added noise power %.6g | noise seed %u",
                            r->noise.requested_snr_db, r->noise.reference_power, r->noise.reference_begin,
                            r->noise.reference_end, r->noise.added_noise_power, r->noise.noise_seed);
        }
        DrawMeasurements(*r);
        DrawPlots();
    }
    else
        ImGui::TextDisabled("No signal generated yet. Choose settings and select Generate Signal.");
    if (two_pane)
        ImGui::EndTable();
}

void SignalGenerator::UpdateSpectrum()
{
    const auto& r = job_.result();
    if (!r)
        return;
    spectrum_ = iq::welch_psd(r->samples, r->sample_rate_hz, 1024, window_);
    spectrum_db_.resize(spectrum_.power_density.size());
    std::transform(spectrum_.power_density.begin(), spectrum_.power_density.end(), spectrum_db_.begin(),
                   [](double p) { return 10 * std::log10(std::max(p, 1e-20)); });
    spectrum_fit_ = true;
}

void SignalGenerator::DrawMeasurements(const iq::GeneratedSignal& r)
{
    ImGui::SeparatorText("Measurements");
    Metric("Mean power", num("%.4g", power_.mean_power), true);
    ImGui::SetItemTooltip("Mean of |x|^2 over every sample, filter transients included.");
    Metric("Peak power", num("%.4g", power_.peak_power));
    Metric("PAPR", num("%.3g dB", power_.papr_db));
    ImGui::SetItemTooltip("Peak-to-average power ratio. A constant-envelope signal has 0 dB; shaped QAM is several dB higher, which is what stresses power amplifiers.");
    if (accuracy_)
    {
        Metric("EVM", num("%.3g %%", 100 * accuracy_->evm_rms), true);
        ImGui::SetItemTooltip("RMS error between the matched-filter observations and the ideal symbols, relative to the RMS ideal symbol. Even a clean RRC signal shows a small floor from the truncated filter.");
        Metric("EVM", num("%.4g dB", accuracy_->evm_db));
        Metric("SNR after matched filter", num("%.4g dB", accuracy_->snr_after_matched_db));
        ImGui::SetItemTooltip("-EVM in dB. Matched filtering averages noise over about SPS samples, so this exceeds the sample-level SNR by 10 log10(SPS).");
        if (r.noise.awgn_applied)
            ImGui::TextWrapped("Requested sample SNR %.4g dB + 10 log10(SPS) = %.4g dB expected after the matched filter. The measured value varies with the noise realization.",
                               r.noise.requested_snr_db, r.noise.requested_snr_db + accuracy_->expected_offset_db);
    }
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
        if (ImPlot::BeginPlot("Complex baseband", ImVec2(-1, -1)))
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
        if (ImPlot::BeginPlot("I/Q constellation", ImVec2(-1, -1), ImPlotFlags_Equal))
        {
            ImPlot::SetupAxes("In-phase (I)", "Quadrature (Q)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, ImVec4(.2f, .6f, 1.f, 1.f));
            ImPlot::PlotScatter("Symbols", data.x.data(), data.y.data(), static_cast<int>(data.x.size()));
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    if (!noise_source && ImGui::BeginTabItem("Eye"))
    {
        ImGui::Combo("Component", &eye_component_, "In-phase (I)\0Quadrature (Q)\0");
        if (eye_.in_phase.empty())
            ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
        else
            ImGui::TextWrapped("%zu overlaid matched-filter traces, two symbol periods wide. A wide-open eye at 0 means easy, error-free decisions; noise and ISI close it.",
                               eye_.in_phase.size());
        if (ImPlot::BeginPlot("Eye diagram", ImVec2(-1, -1)))
        {
            ImPlot::SetupAxes("Time (symbol periods)", eye_component_ == 0 ? "I" : "Q", ImPlotAxisFlags_AutoFit,
                              ImPlotAxisFlags_AutoFit);
            const auto& traces = eye_component_ == 0 ? eye_.in_phase : eye_.quadrature;
            const auto  colour = eye_component_ == 0 ? ImVec4(.2f, .6f, 1.f, .35f) : ImVec4(1.f, .55f, .15f, .35f);
            for (const auto& trace : traces)
            {
                ImPlot::SetNextLineStyle(colour);
                ImPlot::PlotLine("##eye", eye_.time_symbols.data(), trace.data(), static_cast<int>(trace.size()));
            }
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Spectrum"))
    {
        int window = static_cast<int>(window_);
        if (ImGui::Combo("Window", &window, "Hann\0Hamming\0Blackman\0Rectangular\0"))
        {
            window_ = static_cast<iq::Window>(window);
            UpdateSpectrum();
        }
        ImGui::SetItemTooltip("Hann is the default. Rectangular has the narrowest main lobe but the worst leakage; Blackman has the lowest sidelobes with a wider main lobe.");
        ImGui::Text("Two-sided Welch PSD | Periodic %s | %zu samples/segment | %zu segments",
                    iq::window_name(spectrum_.window), spectrum_.segment_length, spectrum_.segment_count);
        ImGui::TextWrapped("Relative power density; no impedance or watt/dBm calibration. Display floor: -200 dB.");
        if (spectrum_db_.empty())
            ImGui::TextUnformatted("At least four samples are needed for a spectrum.");
        else
        {
            if (spectrum_fit_)
                ImPlot::SetNextAxesToFit();
            if (ImPlot::BeginPlot("Baseband PSD", ImVec2(-1, -1)))
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
