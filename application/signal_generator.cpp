#include "signal_generator.h"
#include "help_topics.h"
#include "plot_figures.h"
#include "imgui.h"
#include "implot.h"
#include "preset.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
// "?" button beside a control: opens a panel explaining the setting. `live` adds a line computed from the current settings.
void HelpButton(const help::Topic& topic, const std::string& live = {}, bool inside_disabled = false)
{
    if (inside_disabled)
        ImGui::EndDisabled(); // The explanation stays readable while its control is greyed out.
    ImGui::SameLine();
    ImGui::PushID(topic.id.data(), topic.id.data() + topic.id.size());
    if (ImGui::SmallButton("?"))
        ImGui::OpenPopup("help");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip("What is this? Click for an explanation.");
    if (ImGui::BeginPopup("help"))
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.f);
        ImGui::TextColored(ImVec4(.55f, .75f, 1.f, 1.f), "%.*s", static_cast<int>(topic.title.size()), topic.title.data());
        ImGui::Separator();
        ImGui::TextUnformatted(topic.body.data(), topic.body.data() + topic.body.size());
        if (!live.empty())
        {
            ImGui::Separator();
            ImGui::TextUnformatted(live.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (inside_disabled)
        ImGui::BeginDisabled();
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
                             iq::modulation_name(r.config.modulation),
                             r.family == iq::Family::Fsk ? r.symbol_frequencies_hz.size() : r.symbols.size(), r.samples.size(),
                             r.sample_rate_hz);
            plots_    = {};
            spectrum_ = {};
            spectrum_db_.clear();
            waveform_fit_ = true;
            spectrum_fit_ = true;
            plots_        = make_plot_data(r);
            power_        = iq::power_statistics(r.samples);
            accuracy_     = iq::symbol_accuracy(r);
            eye_          = r.family == iq::Family::Linear ? iq::eye_diagram(r) : iq::EyeDiagram{};
            pipeline_.reset();
            if (r.family == iq::Family::Linear)
                pipeline_ = iq::pipeline_stages(r);
            pipeline_first_ = 0;
            pipeline_fit_   = true;
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
    Hint("Linear: BPSK, QPSK, 8-PSK, 16/32/64/256-QAM, OOK, 4-PAM and 4-ASK carry 1 to 8 bits per symbol with Gray labelling (32-QAM is the cross constellation, only partly Gray); DBPSK, DQPSK, pi/4-DQPSK and 8-DPSK encode the data in phase changes between symbols; OQPSK delays the quadrature stream by half a symbol. 2-FSK, 4-FSK and MSK switch the carrier frequency with continuous phase. WGN is a pure noise source.");
    const auto family       = iq::waveform_family(config_.modulation);
    const bool noise_source = family == iq::Family::Noise;
    const bool fsk          = family == iq::Family::Fsk;
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
        Hint("How many symbols to transmit. Each symbol carries log2(M) bits: 1 for BPSK, OOK, DBPSK, 2-FSK and MSK, 2 for QPSK, OQPSK, 4-PAM, 4-ASK, DQPSK, pi/4-DQPSK and 4-FSK, 3 for 8-PSK and 8-DPSK, 4 for 16-QAM, 5 for 32-QAM, 6 for 64-QAM, 8 for 256-QAM.");
        ImGui::InputDouble("Symbol rate (Bd)", &config_.symbol_rate_baud, 100, 1000, "%.6g");
        Hint("Symbols per second. Together with samples per symbol it sets the sample rate.");
        HelpButton(help::symbol_rate);
        ImGui::InputInt("Samples per symbol", &config_.samples_per_symbol);
        Hint("Oversampling factor (SPS). Higher values give smoother waveforms and more room in the spectrum, at the cost of more samples. RRC needs at least 2; FSK needs enough SPS to keep every tone below half the sample rate.");
        HelpButton(help::sps);
        ImGui::Text("Sample rate: %.6g Hz", config_.symbol_rate_baud * config_.samples_per_symbol);
        ImGui::InputDouble("Amplitude gain", &config_.amplitude_gain, .1, 1, "%.6g");
        Hint("Scales every sample once. Power scales with gain squared.");
        if (fsk)
        {
            const bool locked = config_.modulation == iq::Modulation::MSK;
            ImGui::SeparatorText("Frequency Modulation");
            double spacing = iq::fsk_tone_spacing_hz(config_);
            ImGui::BeginDisabled(locked);
            if (ImGui::InputDouble("Tone spacing (Hz)", &spacing, 50, 500, "%.6g"))
                config_.tone_spacing_hz = spacing;
            ImGui::EndDisabled();
            Hint("Frequency distance between adjacent tones. The modulation index is h = spacing / symbol rate; MSK fixes it at 0.5, the smallest spacing whose tones stay orthogonal over one symbol.");
            ImGui::Text("Modulation index h: %.4g%s", iq::fsk_modulation_index(config_), locked ? " (fixed for MSK)" : "");
            ImGui::TextWrapped("Continuous phase, constant envelope. No pulse filter: output is exactly symbols x SPS samples.");
        }
        else
        {
        ImGui::SeparatorText("Pulse Shaping");
        int pulse = static_cast<int>(config_.pulse);
        if (ImGui::Combo("Pulse", &pulse, "Root-raised cosine\0Rectangular\0"))
            config_.pulse = static_cast<iq::Pulse>(pulse);
        Hint("RRC is the practical choice: with a matching receive filter it has no inter-symbol interference at the decision instants and a compact spectrum. Rectangular pulses open the eye fully but have wide sinc sidelobes.");
        HelpButton(help::pulse);
        ImGui::BeginDisabled(config_.pulse != iq::Pulse::RRC);
        ImGui::InputDouble("RRC roll-off", &config_.roll_off, .05, .1, "%.4g");
        Hint("Excess bandwidth of the root-raised-cosine filter, from 0 to 1. Occupied bandwidth is about symbol rate x (1 + roll-off). Low values give narrow spectra but longer, more sensitive filter tails.");
        HelpButton(help::roll_off, {}, config_.pulse != iq::Pulse::RRC);
        ImGui::InputInt("RRC span (symbols)", &config_.span_symbols);
        Hint("Filter length in symbols. Longer filters approximate the ideal response better (less residual ISI) and cost more samples.");
        HelpButton(help::span, {}, config_.pulse != iq::Pulse::RRC);
        ImGui::EndDisabled();
        }
        ImGui::SeparatorText("AWGN");
        ImGui::Checkbox("Add AWGN", &config_.awgn.enabled);
        Hint("Additive white Gaussian noise at the SNR below. Disabled keeps the clean signal.");
        ImGui::BeginDisabled(!config_.awgn.enabled);
        ImGui::InputDouble("SNR (dB)", &config_.awgn.snr_db, 1, 10, "%.6g");
        Hint("Clean signal power divided by added noise power, in dB. Lower values spread the constellation and close the eye.");
        {
            const auto ratios = iq::snr_to_energy_ratios(config_.awgn.snr_db, std::max(config_.samples_per_symbol, 1),
                                                         std::max(iq::bits_per_symbol(config_.modulation), 1));
            HelpButton(help::snr, num("With your settings: SNR %.4g dB", config_.awgn.snr_db) + num(" = Es/N0 %.4g dB", ratios.es_n0_db) +
                                      num(" = Eb/N0 %.4g dB", ratios.eb_n0_db),
                       !config_.awgn.enabled);
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("SNR is clean sample power over added complex noise power, measured on the steady-state interval. Es/N0 = SNR + 10 log10(SPS); Eb/N0 = Es/N0 - 10 log10(bits per symbol).");
        if (config_.awgn.enabled && std::isfinite(config_.awgn.snr_db))
        {
            const auto ratios = iq::snr_to_energy_ratios(config_.awgn.snr_db, std::max(config_.samples_per_symbol, 1),
                                                         std::max(iq::bits_per_symbol(config_.modulation), 1));
            ImGui::Text("Equivalent: Es/N0 %.4g dB | Eb/N0 %.4g dB", ratios.es_n0_db, ratios.eb_n0_db);
        }
        ImGui::SeparatorText("Channel impairments");
        auto& imp = config_.impairments;
        ImGui::InputDouble("CFO (Hz)", &imp.cfo_hz, 1, 10, "%.6g");
        Hint("Carrier frequency offset. The constellation spins at this rate; a receiver needs carrier recovery to stop it.");
        ImGui::InputDouble("Phase noise linewidth (Hz)", &imp.phase_noise_linewidth_hz, 0.1, 1, "%.6g");
        Hint("3 dB linewidth of a free-running oscillator. The phase random-walks, smearing each constellation point into an arc.");
        ImGui::InputDouble("IQ gain imbalance (dB)", &imp.iq_gain_db, 0.1, 1, "%.6g");
        Hint("Gain of the Q branch relative to I. Squeezes the constellation along one axis and leaves an image of the signal in the spectrum.");
        ImGui::InputDouble("IQ phase skew (deg)", &imp.iq_phase_deg, 0.5, 5, "%.6g");
        Hint("Quadrature error: the Q branch leaks a little of I, shearing the constellation.");
        ImGui::InputDouble("DC offset I (x RMS)", &imp.dc_offset_i, 0.01, 0.1, "%.6g");
        ImGui::InputDouble("DC offset Q (x RMS)", &imp.dc_offset_q, 0.01, 0.1, "%.6g");
        Hint("A constant added to I or Q, as a fraction of the signal RMS amplitude. Shifts the whole constellation and adds a spectral line at 0 Hz.");
        ImGui::InputInt("ADC bits", &imp.adc_bits);
        Hint("Quantizer resolution per component, 2 to 24; 0 disables it. The full scale auto-ranges to the largest I or Q magnitude. Few bits give a staircase waveform and a grid-like constellation.");
        ImGui::TextWrapped("Applied after AWGN, in the order: CFO, phase noise, IQ imbalance, DC offset, quantization.");
        ImGui::InputScalar("Impairment seed", ImGuiDataType_U32, &config_.impairment_seed);
        Hint("Seeds the phase-noise random walk; it is independent of the data and AWGN seeds.");
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
    DrawImageExportDialog();
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
            Metric("Symbols", std::to_string(r->family == iq::Family::Fsk ? r->symbol_frequencies_hz.size() : r->symbols.size()));
            Metric("Samples", std::to_string(r->samples.size()));
            Metric("Sample rate", num("%.6g Hz", r->sample_rate_hz));
            Metric("Duration", num("%.6g s", r->samples.size() / r->sample_rate_hz));
            if (r->family == iq::Family::Fsk)
                Metric("Modulation index", num("%.4g", iq::fsk_modulation_index(r->config)), true);
            else
                Metric("Filter delay", std::to_string(r->filter_delay_samples) + " samples", true);
            Metric("Gain", num("%.6g", r->config.amplitude_gain));
            if (r->noise.awgn_applied)
                ImGui::TextWrapped("AWGN: requested %.6g dB | reference power %.6g over [%zu,%zu) | added noise power %.6g | noise seed %u",
                            r->noise.requested_snr_db, r->noise.reference_power, r->noise.reference_begin,
                            r->noise.reference_end, r->noise.added_noise_power, r->noise.noise_seed);
            if (r->impairments_applied)
                ImGui::TextWrapped("Impairments: CFO %.6g Hz | phase noise %.6g Hz | IQ %.6g dB / %.6g deg | DC %.6g%+.6gj x RMS | ADC %d bits | seed %u",
                            r->config.impairments.cfo_hz, r->config.impairments.phase_noise_linewidth_hz,
                            r->config.impairments.iq_gain_db, r->config.impairments.iq_phase_deg,
                            r->config.impairments.dc_offset_i, r->config.impairments.dc_offset_q,
                            r->config.impairments.adc_bits, r->config.impairment_seed);
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
    Hint("Mean of |x|^2 over every sample, filter transients included.");
    Metric("Peak power", num("%.4g", power_.peak_power));
    Metric("PAPR", num("%.3g dB", power_.papr_db));
    Hint("Peak-to-average power ratio. A constant-envelope signal has 0 dB; shaped QAM is several dB higher, which is what stresses power amplifiers.");
    if (accuracy_)
    {
        Metric("EVM", num("%.3g %%", 100 * accuracy_->evm_rms), true);
        Hint("RMS error between the matched-filter observations and the ideal symbols, relative to the RMS ideal symbol. Even a clean RRC signal shows a small floor from the truncated filter.");
        Metric("EVM", num("%.4g dB", accuracy_->evm_db));
        Metric("SNR after matched filter", num("%.4g dB", accuracy_->snr_after_matched_db));
        Hint("-EVM in dB. Matched filtering averages noise over about SPS samples, so this exceeds the sample-level SNR by 10 log10(SPS).");
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
        ExportImageButton("waveform", [&] { return figures::waveform(plots_); });
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
    const bool fsk_source   = result && result->family == iq::Family::Fsk;
    if (fsk_source && ImGui::BeginTabItem("Frequency"))
    {
        ExportImageButton("frequency", [&] { return figures::frequency(plots_); });
        ImGui::TextWrapped("Blue: frequency estimated from the phase step between consecutive samples. Orange: nominal tone of each symbol. A continuous-phase signal moves between tones without phase jumps; noise and CFO shift the estimate.");
        if (ImPlot::BeginPlot("Instantaneous frequency", ImVec2(-1, -1)))
        {
            ImPlot::SetupAxes("Time (s)", "Frequency (Hz)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextLineStyle(ImVec4(.2f, .6f, 1.f, 1.f));
            ImPlot::PlotLine("Estimate", plots_.freq_time.data(), plots_.freq_estimate.data(),
                             static_cast<int>(plots_.freq_time.size()));
            ImPlot::SetNextLineStyle(ImVec4(1.f, .55f, .15f, 1.f), 2.f);
            ImPlot::PlotLine("Nominal tone", plots_.freq_time.data(), plots_.freq_nominal.data(),
                             static_cast<int>(plots_.freq_time.size()));
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    if (!noise_source && !fsk_source && ImGui::BeginTabItem("Constellation"))
    {
        const bool noisy = result && (result->noise.awgn_applied || result->impairments_applied);
        ImGui::Combo("View", &constellation_view_,
                     noisy ? "Mapped symbols (ideal, gain applied)\0Matched filter (degraded observations)\0"
                           : "Mapped symbols (gain applied)\0Matched filter (steady-state symbols)\0");
        const auto& data = constellation_view_ == 0 ? plots_.mapped : plots_.matched;
        if (data.x.empty())
            ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
        else
            ExportImageButton("constellation", [&] {
                return figures::constellation(data, constellation_view_ == 0 ? "I/Q constellation (mapped symbols)"
                                                                           : "I/Q constellation (matched filter)");
            });
        if (ImPlot::BeginPlot("I/Q constellation", ImVec2(-1, -1), ImPlotFlags_Equal))
        {
            ImPlot::SetupAxes("In-phase (I)", "Quadrature (Q)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, ImVec4(.2f, .6f, 1.f, 1.f));
            ImPlot::PlotScatter("Symbols", data.x.data(), data.y.data(), static_cast<int>(data.x.size()));
            ImPlot::EndPlot();
        }
        ImGui::EndTabItem();
    }
    if (!noise_source && !fsk_source && ImGui::BeginTabItem("Eye"))
    {
        ImGui::SetNextItemWidth(220);
        ImGui::Combo("Component", &eye_component_, "In-phase (I)\0Quadrature (Q)\0");
        if (eye_.in_phase.empty())
            ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
        else
        {
            ImGui::SameLine();
            ExportImageButton("eye", [&] { return figures::eye(eye_, eye_component_ == 1); });
            ImGui::TextWrapped("%zu overlaid matched-filter traces, two symbol periods wide. A wide-open eye at 0 means easy, error-free decisions; noise and ISI close it.",
                               eye_.in_phase.size());
        }
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
    if (!noise_source && !fsk_source && result && pipeline_ && ImGui::BeginTabItem("Pipeline"))
    {
        DrawPipeline(*result);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Spectrum"))
    {
        int window = static_cast<int>(window_);
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("Window", &window, "Hann\0Hamming\0Blackman\0Rectangular\0"))
        {
            window_ = static_cast<iq::Window>(window);
            UpdateSpectrum();
        }
        if (!spectrum_db_.empty())
        {
            ImGui::SameLine();
            ExportImageButton("spectrum", [&] { return figures::spectrum(spectrum_, spectrum_db_); });
        }
        Hint("Hann is the default. Rectangular has the narrowest main lobe but the worst leakage; Blackman has the lowest sidelobes with a wider main lobe.");
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

void SignalGenerator::DrawPipeline(const iq::GeneratedSignal& r)
{
    const auto& p = *pipeline_;
    const int   total = static_cast<int>(p.symbols.size());
    const int   sps   = p.samples_per_symbol;
    const int   bps   = p.bits_per_symbol;
    ExportImageButton("pipeline", [&] { return figures::pipeline(p, pipeline_first_, pipeline_count_, pipeline_align_); });
    ImGui::TextWrapped("One transmission, step by step, on a shared time axis (symbol periods). I: blue | Q: orange");
    HelpButton(help::pipeline);
    bool moved = false;
    ImGui::SetNextItemWidth(220);
    pipeline_count_ = std::clamp(pipeline_count_, std::min(4, total), std::min(64, total));
    moved |= ImGui::SliderInt("Symbols shown", &pipeline_count_, std::min(4, total), std::min(64, total));
    Hint("How many symbols the rows cover. Fewer symbols make each step easier to read.");
    const int max_first = std::max(0, total - pipeline_count_);
    pipeline_first_     = std::clamp(pipeline_first_, 0, max_first);
    ImGui::SetNextItemWidth(220);
    moved |= ImGui::SliderInt("First symbol", &pipeline_first_, 0, max_first);
    Hint("Index of the first symbol shown. Scroll through the signal; the start and end show the filter ramping up and down.");
    ImGui::BeginDisabled(p.filter_delay_samples == 0);
    ImGui::Checkbox("Compensate filter delay", &pipeline_align_);
    ImGui::EndDisabled();
    Hint("The pulse filter delays its output by half its span. With this on, steps 4 and 5 are shifted back so each pulse peak lines up with its symbol in steps 2 and 3.");
    if (p.filter_delay_samples > 0)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(delay %.4g symbols)", static_cast<double>(p.filter_delay_samples) / sps);
    }
    if (moved)
        pipeline_fit_ = true;
    const int  count  = pipeline_count_;
    const int  first  = pipeline_first_;
    const bool align  = pipeline_align_ && p.filter_delay_samples > 0;
    const auto offset = align ? p.filter_delay_samples : 0;
    // Samples [first*SPS, (first+count)*SPS) of a stage, with x in symbol periods.
    struct Series { std::vector<double> x, i, q; };
    auto series = [&](const std::vector<std::complex<float>>& stage, std::size_t shift, bool closed) {
        Series s;
        const auto begin = static_cast<std::size_t>(first) * static_cast<std::size_t>(sps) + shift;
        const auto n     = static_cast<std::size_t>(count) * static_cast<std::size_t>(sps) + (closed ? 1 : 0);
        for (std::size_t k = 0; k < n && begin + k < stage.size(); ++k)
        {
            s.x.push_back(first + static_cast<double>(k) / sps);
            s.i.push_back(stage[begin + k].real());
            s.q.push_back(stage[begin + k].imag());
        }
        return s;
    };
    auto start_plot = [&](const char* title, bool last, double ylo, double yhi) {
        if (!ImPlot::BeginPlot(title, ImVec2(0, 0), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText))
            return false;
        ImPlot::SetupAxes(last ? "Time (symbol periods)" : nullptr, nullptr, 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, first, first + count, pipeline_fit_ ? ImPlotCond_Always : ImPlotCond_Once);
        if (ylo < yhi)
            ImPlot::SetupAxisLimits(ImAxis_Y1, ylo, yhi, ImPlotCond_Always);
        return true;
    };
    const ImVec4 blue(.2f, .6f, 1.f, 1.f), orange(1.f, .55f, .15f, 1.f);
    // Shared vertical scale for the sample-level rows so that amplitude changes between steps are visible.
    double peak = 1e-9;
    for (const auto* stage : {&p.symbols, &p.upsampled, &p.shaped, &p.received})
        for (const auto& v : *stage)
            peak = std::max({peak, static_cast<double>(std::abs(v.real())), static_cast<double>(std::abs(v.imag()))});
    const double ymax = peak * 1.25;
    if (ImPlot::BeginSubplots("##pipeline", 5, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle))
    {
        if (start_plot("1. Data bits", false, -.3, 1.3))
        {
            std::vector<double> x, y;
            const int           bit_begin = first * bps, bit_end = std::min(static_cast<int>(p.bits.size()), (first + count) * bps);
            for (int b = bit_begin; b < bit_end; ++b)
            {
                x.push_back(static_cast<double>(b) / bps);
                y.push_back(p.bits[static_cast<std::size_t>(b)] == '1' ? 1. : 0.);
            }
            if (!x.empty())
            {
                x.push_back(static_cast<double>(bit_end) / bps);
                y.push_back(y.back());
                ImPlot::SetNextLineStyle(blue, 2.f);
                ImPlot::PlotStairs("bits", x.data(), y.data(), static_cast<int>(x.size()));
                if (bit_end - bit_begin <= 96)
                    for (int b = bit_begin; b < bit_end; ++b)
                        ImPlot::PlotText(p.bits[static_cast<std::size_t>(b)] == '1' ? "1" : "0", (b + .5) / bps, .5);
            }
            ImPlot::EndPlot();
        }
        if (start_plot("2. Mapped symbols (one complex value per symbol)", false, -ymax, ymax))
        {
            std::vector<double> x, yi, yq;
            for (int k = first; k < first + count; ++k)
            {
                x.push_back(k);
                yi.push_back(p.symbols[static_cast<std::size_t>(k)].real());
                yq.push_back(p.symbols[static_cast<std::size_t>(k)].imag());
            }
            x.push_back(first + count);
            yi.push_back(yi.back());
            yq.push_back(yq.back());
            ImPlot::SetNextLineStyle(blue, 2.f);
            ImPlot::PlotStairs("I", x.data(), yi.data(), static_cast<int>(x.size()));
            ImPlot::SetNextLineStyle(orange, 2.f);
            ImPlot::PlotStairs("Q", x.data(), yq.data(), static_cast<int>(x.size()));
            ImPlot::EndPlot();
        }
        if (start_plot("3. Zeros inserted between symbols (upsampling by SPS)", false, -ymax, ymax))
        {
            const auto s = series(p.upsampled, 0, false);
            ImPlot::SetNextLineStyle(blue, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.5f, blue);
            ImPlot::PlotStems("I", s.x.data(), s.i.data(), static_cast<int>(s.x.size()));
            ImPlot::SetNextLineStyle(orange, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.5f, orange);
            ImPlot::PlotStems("Q", s.x.data(), s.q.data(), static_cast<int>(s.x.size()));
            ImPlot::EndPlot();
        }
        const std::string shaped_title =
            std::string("4. After the pulse filter (") + (r.config.pulse == iq::Pulse::RRC ? "root-raised cosine" : "rectangular") + ")";
        if (start_plot(shaped_title.c_str(), false, -ymax, ymax))
        {
            const auto s = series(p.shaped, offset, true);
            ImPlot::SetNextLineStyle(blue, 1.5f);
            ImPlot::PlotLine("I", s.x.data(), s.i.data(), static_cast<int>(s.x.size()));
            ImPlot::SetNextLineStyle(orange, 1.5f);
            ImPlot::PlotLine("Q", s.x.data(), s.q.data(), static_cast<int>(s.x.size()));
            ImPlot::EndPlot();
        }
        const char* noisy_title = p.degraded ? "5. With noise and impairments (the exported signal)"
                                             : "5. With noise: none added yet (enable AWGN or an impairment and generate)";
        if (start_plot(noisy_title, true, -ymax, ymax))
        {
            const auto s = series(p.received, offset, true);
            ImPlot::SetNextLineStyle(blue, 1.5f);
            ImPlot::PlotLine("I", s.x.data(), s.i.data(), static_cast<int>(s.x.size()));
            ImPlot::SetNextLineStyle(orange, 1.5f);
            ImPlot::PlotLine("Q", s.x.data(), s.q.data(), static_cast<int>(s.x.size()));
            ImPlot::EndPlot();
        }
        ImPlot::EndSubplots();
    }
    pipeline_fit_ = false;
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
    if (ImGui::Combo("Format", &export_format_, "CSV\0Binary float32 I/Q\0SigMF (cf32 + .sigmf-meta)\0"))
        confirm_overwrite_ = false;
    ImGui::TextWrapped("Metadata is written beside the samples as <destination>.json (SigMF: use a .sigmf-data destination; <name>.sigmf-meta is written beside it).");
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

void SignalGenerator::ExportImageButton(const char* stem, const std::function<iq::Figure()>& build)
{
    if (ImGui::Button("Export image..."))
    {
        try
        {
            image_figure_ = build();
            std::snprintf(image_path_, sizeof image_path_, "siggen-%s.%s", stem, image_format_ == 0 ? "png" : "svg");
            image_status_.clear();
            image_confirm_overwrite_ = false;
            image_open_requested_    = true;
        }
        catch (const std::exception& e)
        {
            spdlog::error("Unable to prepare image: {}", e.what());
            error_ = e.what();
        }
    }
    Hint("Save this plot as a PNG or SVG image for slides and reports. The image is drawn from the plotted data, so it does not depend on the window size.");
}
void SignalGenerator::ExportImage(bool overwrite)
{
    try
    {
        iq::ImageStyle style;
        style.width  = image_width_;
        style.height = image_height_;
        style.dark   = image_theme_ == 0;
        iq::export_figure(image_path_, *image_figure_, image_format_ == 0 ? iq::ImageFormat::PNG : iq::ImageFormat::SVG, style, overwrite);
        spdlog::info("Exported image {} ({}x{})", image_path_, style.width, style.height);
        image_status_            = std::string("Saved ") + image_path_;
        image_confirm_overwrite_ = false;
    }
    catch (const std::exception& e)
    {
        spdlog::error("Image export to {} failed: {}", image_path_, e.what());
        image_status_ = std::string("Export failed: ") + e.what();
    }
}
void SignalGenerator::DrawImageExportDialog()
{
    if (image_open_requested_)
    {
        ImGui::OpenPopup("Export image");
        image_open_requested_ = false;
    }
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export image", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextWrapped("Save the plot as an image. PNG is a raster for slides; SVG is vector and stays sharp at any size in documents.");
    if (ImGui::InputText("Destination", image_path_, sizeof image_path_))
        image_confirm_overwrite_ = false;
    if (ImGui::Combo("Format", &image_format_, "PNG\0SVG\0"))
    {
        // Keep the name, swap the extension.
        auto path = std::filesystem::path(image_path_);
        path.replace_extension(image_format_ == 0 ? ".png" : ".svg");
        std::snprintf(image_path_, sizeof image_path_, "%s", path.string().c_str());
        image_confirm_overwrite_ = false;
    }
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Width (px)", &image_width_, 100, 400);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Height (px)", &image_height_, 100, 400);
    image_width_  = std::clamp(image_width_, iq::MIN_IMAGE_SIZE, iq::MAX_IMAGE_SIZE);
    image_height_ = std::clamp(image_height_, iq::MIN_IMAGE_SIZE, iq::MAX_IMAGE_SIZE);
    ImGui::Combo("Background", &image_theme_, "Dark (matches the app)\0Light (for print)\0");
    if (ImGui::Button("Save image"))
    {
        try
        {
            image_confirm_overwrite_ = std::filesystem::exists(image_path_);
            if (!image_confirm_overwrite_)
                ExportImage(false);
        }
        catch (const std::exception& e)
        {
            image_status_ = e.what();
        }
    }
    if (image_confirm_overwrite_)
    {
        ImGui::TextWrapped("The destination exists. Replace it?");
        if (ImGui::Button("Confirm overwrite"))
            ExportImage(true);
        ImGui::SameLine();
        if (ImGui::Button("Cancel overwrite"))
            image_confirm_overwrite_ = false;
    }
    if (!image_status_.empty())
        ImGui::TextWrapped("%s", image_status_.c_str());
    if (ImGui::Button("Close"))
    {
        image_figure_.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}
