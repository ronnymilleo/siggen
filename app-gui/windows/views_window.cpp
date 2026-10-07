/**
 * @file    views_window.cpp
 * @brief   The Signal Views window: plots of the last result and their image export.
 */

#include "views_window.h"

#include "help_topics.h"
#include "implot.h"
#include "plot_figures.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <vector>

namespace GUI {

/**
 * @brief   Creates the window over a session.
 * @param[in,out] session  The session whose result the window plots; must outlive the window. The window changes
 *                         only its spectrum window and its error message.
 */
ViewsWindow::ViewsWindow(GeneratorSession &session) : AppWindow("Signal Views"), m_Session(session) {
}

/**
 * @brief   Draws the tab bar of plots and the image export dialog; each tab appears only for the waveform families
 *          it applies to.
 */
void ViewsWindow::Draw() {
    FollowSession();
    const auto &result = m_Session.GetResult();
    if (!result) {
        ImGui::TextDisabled("Plots of the generated signal appear here.");
        return;
    }
    if (ImGui::BeginTabBar("Signal views")) {
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(.15f, .15f));
        DrawWaveformTab();
        const bool noise_source = result->Family == Core::Family::Noise;
        const bool fsk_source = result->Family == Core::Family::Fsk;
        const bool symbol_views = !noise_source && !fsk_source;
        if (fsk_source) {
            DrawFrequencyTab();
        }
        if (symbol_views) {
            DrawConstellationTab(result->Noise.AwgnApplied || result->ImpairmentsApplied);
            DrawEyeTab();
        }
        if (symbol_views && m_Session.GetPipeline()) {
            DrawPipelineTab(*result);
        }
        DrawSpectrumTab();
        ImPlot::PopStyleVar();
        ImGui::EndTabBar();
    }
    DrawImageExportDialog();
}

/**
 * @brief   Resets the view state when the session analysed a new result or recomputed the spectrum.
 * @note    A new result refits the plots and moves the pipeline view back to the first symbol.
 */
void ViewsWindow::FollowSession() {
    if (m_SeenResultVersion != m_Session.GetResultVersion()) {
        m_SeenResultVersion = m_Session.GetResultVersion();
        m_WaveformFit = true;
        m_PipelineFirst = 0;
        m_PipelineFit = true;
    }
    if (m_SeenSpectrumVersion != m_Session.GetSpectrumVersion()) {
        m_SeenSpectrumVersion = m_Session.GetSpectrumVersion();
        m_SpectrumFit = true;
    }
}

void ViewsWindow::DrawWaveformTab() {
    const auto &plots = m_Session.GetPlots();
    if (!ImGui::BeginTabItem("Waveform")) {
        return;
    }
    ExportImageButton("waveform", [&] { return Core::WaveformFigure(plots); });
    ImGui::Text("I: blue | Q: orange | %zu plotted points (min/max reduction)", plots.Time.size());
    if (m_WaveformFit) {
        ImPlot::SetNextAxesToFit();
    }
    if (ImPlot::BeginPlot("Complex baseband", ImVec2(-1, -1))) {
        m_WaveformFit = false;
        ImPlot::SetupAxes("Time (s)", "Amplitude");
        ImPlot::SetNextLineStyle(InPhaseColor);
        ImPlot::PlotLine("I", plots.Time.data(), plots.I.data(), static_cast<int>(plots.Time.size()));
        ImPlot::SetNextLineStyle(QuadratureColor);
        ImPlot::PlotLine("Q", plots.Time.data(), plots.Q.data(), static_cast<int>(plots.Time.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void ViewsWindow::DrawFrequencyTab() {
    const auto &plots = m_Session.GetPlots();
    if (!ImGui::BeginTabItem("Frequency")) {
        return;
    }
    ExportImageButton("frequency", [&] { return Core::FrequencyFigure(plots); });
    ImGui::TextWrapped("Blue: frequency estimated from the phase step between consecutive samples. Orange: nominal "
                       "tone of each symbol. A continuous-phase signal moves between tones without phase jumps; "
                       "noise and CFO shift the estimate.");
    if (ImPlot::BeginPlot("Instantaneous frequency", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (s)", "Frequency (Hz)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetNextLineStyle(InPhaseColor);
        ImPlot::PlotLine("Estimate", plots.FreqTime.data(), plots.FreqEstimate.data(),
                         static_cast<int>(plots.FreqTime.size()));
        ImPlot::SetNextLineStyle(QuadratureColor, 2.f);
        ImPlot::PlotLine("Nominal tone", plots.FreqTime.data(), plots.FreqNominal.data(),
                         static_cast<int>(plots.FreqTime.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

/**
 * @brief   Draws the constellation tab, with the mapped symbols or the matched-filter observations.
 * @param[in] noisy  True when AWGN or impairments were applied; only changes the labels of the view selector.
 */
void ViewsWindow::DrawConstellationTab(bool noisy) {
    const auto &plots = m_Session.GetPlots();
    if (!ImGui::BeginTabItem("Constellation")) {
        return;
    }
    ImGui::Combo("View", &m_ConstellationView,
                 noisy ? "Mapped symbols (ideal, gain applied)\0Matched filter (degraded observations)\0"
                       : "Mapped symbols (gain applied)\0Matched filter (steady-state symbols)\0");
    const auto &data = m_ConstellationView == 0 ? plots.Mapped : plots.Matched;
    if (data.X.empty()) {
        ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
    } else {
        ExportImageButton("constellation", [&] {
            return Core::ConstellationFigure(data, m_ConstellationView == 0 ? "I/Q constellation (mapped symbols)"
                                                                            : "I/Q constellation (matched filter)");
        });
    }
    if (ImPlot::BeginPlot("I/Q constellation", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("In-phase (I)", "Quadrature (Q)", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, InPhaseColor);
        ImPlot::PlotScatter("Symbols", data.X.data(), data.Y.data(), static_cast<int>(data.X.size()));
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void ViewsWindow::DrawEyeTab() {
    const auto &eye = m_Session.GetEyeDiagram();
    if (!ImGui::BeginTabItem("Eye")) {
        return;
    }
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("Component", &m_EyeComponent, "In-phase (I)\0Quadrature (Q)\0");
    if (eye.InPhase.empty()) {
        ImGui::TextWrapped("No steady-state symbols: increase symbol count beyond twice the RRC span.");
    } else {
        ImGui::SameLine();
        ExportImageButton("eye", [&] { return Core::EyeFigure(eye, m_EyeComponent == 1); });
        ImGui::TextWrapped("%zu overlaid matched-filter traces, two symbol periods wide. A wide-open eye at 0 "
                           "means easy, error-free decisions; noise and ISI close it.",
                           eye.InPhase.size());
    }
    if (ImPlot::BeginPlot("Eye diagram", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("Time (symbol periods)", m_EyeComponent == 0 ? "I" : "Q", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        const auto &traces = m_EyeComponent == 0 ? eye.InPhase : eye.Quadrature;
        const auto colour = m_EyeComponent == 0 ? ImVec4(.2f, .6f, 1.f, .35f) : ImVec4(1.f, .55f, .15f, .35f);
        for (const auto &trace : traces) {
            ImPlot::SetNextLineStyle(colour);
            ImPlot::PlotLine("##eye", eye.TimeSymbols.data(), trace.data(), static_cast<int>(trace.size()));
        }
        ImPlot::EndPlot();
    }
    ImGui::EndTabItem();
}

void ViewsWindow::DrawPipelineTab(const Core::GeneratedSignal &result) {
    if (!ImGui::BeginTabItem("Pipeline")) {
        return;
    }
    DrawPipelineControls();
    DrawPipelineStages(result);
    ImGui::EndTabItem();
}

/**
 * @brief   Draws the export button, explanation and window sliders of the pipeline view.
 * @note    Clamps the window to the available symbols and requests a refit when the user moves it.
 */
void ViewsWindow::DrawPipelineControls() {
    const auto &stages = *m_Session.GetPipeline();
    const int total = static_cast<int>(stages.Symbols.size());
    ExportImageButton("pipeline",
                      [&] { return Core::PipelineFigure(stages, m_PipelineFirst, m_PipelineCount, m_PipelineAlign); });
    ImGui::TextWrapped("One transmission, step by step, on a shared time axis (symbol periods). I: blue | Q: orange");
    HelpButton(PipelineHelp);
    bool moved = false;
    ImGui::SetNextItemWidth(220);
    m_PipelineCount = std::clamp(m_PipelineCount, std::min(4, total), std::min(64, total));
    moved |= ImGui::SliderInt("Symbols shown", &m_PipelineCount, std::min(4, total), std::min(64, total));
    Hint("How many symbols the rows cover. Fewer symbols make each step easier to read.");
    const int max_first = std::max(0, total - m_PipelineCount);
    m_PipelineFirst = std::clamp(m_PipelineFirst, 0, max_first);
    ImGui::SetNextItemWidth(220);
    moved |= ImGui::SliderInt("First symbol", &m_PipelineFirst, 0, max_first);
    Hint("Index of the first symbol shown. Scroll through the signal; the start and end show the filter ramping up and "
         "down.");
    ImGui::BeginDisabled(stages.FilterDelaySamples == 0);
    ImGui::Checkbox("Compensate filter delay", &m_PipelineAlign);
    ImGui::EndDisabled();
    Hint("The pulse filter delays its output by half its span. With this on, steps 4 and 5 are shifted back so each "
         "pulse peak lines up with its symbol in steps 2 and 3.");
    if (stages.FilterDelaySamples > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(delay %.4g symbols)",
                            static_cast<double>(stages.FilterDelaySamples) / stages.SamplesPerSymbol);
    }
    if (moved) {
        m_PipelineFit = true;
    }
}

/**
 * @brief   Draws the five pipeline rows over the selected window of symbols, on a shared time axis.
 * @param[in] result  The signal the stages were built from; gives the pulse name of row 4.
 */
void ViewsWindow::DrawPipelineStages(const Core::GeneratedSignal &result) {
    const auto &stages = *m_Session.GetPipeline();
    const int sps = stages.SamplesPerSymbol;
    const int bits_per_symbol = stages.BitsPerSymbol;
    const int count = m_PipelineCount;
    const int first = m_PipelineFirst;
    const bool align = m_PipelineAlign && stages.FilterDelaySamples > 0;
    const auto offset = align ? stages.FilterDelaySamples : 0;
    struct Series {
        std::vector<double> X, I, Q;
    };
    // Samples [first*SPS, (first+count)*SPS) of a stage, with x in symbol periods
    auto window_of = [&](const std::vector<std::complex<float>> &stage, std::size_t shift, bool closed) {
        Series series;
        const auto begin = static_cast<std::size_t>(first) * static_cast<std::size_t>(sps) + shift;
        const auto length = static_cast<std::size_t>(count) * static_cast<std::size_t>(sps) + (closed ? 1 : 0);
        for (std::size_t k = 0; k < length && begin + k < stage.size(); ++k) {
            series.X.push_back(first + static_cast<double>(k) / sps);
            series.I.push_back(stage[begin + k].real());
            series.Q.push_back(stage[begin + k].imag());
        }
        return series;
    };
    auto start_plot = [&](const char *title, bool last, double y_min, double y_max) {
        if (!ImPlot::BeginPlot(title, ImVec2(0, 0), ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText)) {
            return false;
        }
        ImPlot::SetupAxes(last ? "Time (symbol periods)" : nullptr, nullptr, 0, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, first, first + count, m_PipelineFit ? ImPlotCond_Always : ImPlotCond_Once);
        if (y_min < y_max) {
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);
        }
        return true;
    };
    // Shared vertical scale for the sample-level rows so that amplitude changes between steps are visible
    double peak = 1e-9;
    for (const auto *stage : {&stages.Symbols, &stages.Upsampled, &stages.Shaped, &stages.Received}) {
        for (const auto &sample : *stage) {
            peak = std::max(
                {peak, static_cast<double>(std::abs(sample.real())), static_cast<double>(std::abs(sample.imag()))});
        }
    }
    const double y_limit = peak * 1.25;
    if (ImPlot::BeginSubplots("##pipeline", 5, 1, ImVec2(-1, -1),
                              ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoTitle)) {
        if (start_plot("1. Data bits", false, -.3, 1.3)) {
            std::vector<double> x, y;
            const int bit_begin = first * bits_per_symbol;
            const int bit_end = std::min(static_cast<int>(stages.Bits.size()), (first + count) * bits_per_symbol);
            for (int b = bit_begin; b < bit_end; ++b) {
                x.push_back(static_cast<double>(b) / bits_per_symbol);
                y.push_back(stages.Bits[static_cast<std::size_t>(b)] == '1' ? 1. : 0.);
            }
            if (!x.empty()) {
                x.push_back(static_cast<double>(bit_end) / bits_per_symbol);
                y.push_back(y.back());
                ImPlot::SetNextLineStyle(InPhaseColor, 2.f);
                ImPlot::PlotStairs("bits", x.data(), y.data(), static_cast<int>(x.size()));
                if (bit_end - bit_begin <= 96) {
                    for (int b = bit_begin; b < bit_end; ++b) {
                        ImPlot::PlotText(stages.Bits[static_cast<std::size_t>(b)] == '1' ? "1" : "0",
                                         (b + .5) / bits_per_symbol, .5);
                    }
                }
            }
            ImPlot::EndPlot();
        }
        if (start_plot("2. Mapped symbols (one complex value per symbol)", false, -y_limit, y_limit)) {
            std::vector<double> x, yi, yq;
            for (int k = first; k < first + count; ++k) {
                x.push_back(k);
                yi.push_back(stages.Symbols[static_cast<std::size_t>(k)].real());
                yq.push_back(stages.Symbols[static_cast<std::size_t>(k)].imag());
            }
            x.push_back(first + count);
            yi.push_back(yi.back());
            yq.push_back(yq.back());
            ImPlot::SetNextLineStyle(InPhaseColor, 2.f);
            ImPlot::PlotStairs("I", x.data(), yi.data(), static_cast<int>(x.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 2.f);
            ImPlot::PlotStairs("Q", x.data(), yq.data(), static_cast<int>(x.size()));
            ImPlot::EndPlot();
        }
        if (start_plot("3. Zeros inserted between symbols (upsampling by SPS)", false, -y_limit, y_limit)) {
            const auto series = window_of(stages.Upsampled, 0, false);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.5f, InPhaseColor);
            ImPlot::PlotStems("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 2.5f, QuadratureColor);
            ImPlot::PlotStems("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        const std::string shaped_title =
            std::string("4. After the pulse filter (") +
            (result.Config.Pulse == Core::Pulse::RRC ? "root-raised cosine" : "rectangular") + ")";
        if (start_plot(shaped_title.c_str(), false, -y_limit, y_limit)) {
            const auto series = window_of(stages.Shaped, offset, true);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::PlotLine("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::PlotLine("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        const char *noisy_title = stages.Degraded
                                      ? "5. With noise and impairments (the exported signal)"
                                      : "5. With noise: none added yet (enable AWGN or an impairment and generate)";
        if (start_plot(noisy_title, true, -y_limit, y_limit)) {
            const auto series = window_of(stages.Received, offset, true);
            ImPlot::SetNextLineStyle(InPhaseColor, 1.5f);
            ImPlot::PlotLine("I", series.X.data(), series.I.data(), static_cast<int>(series.X.size()));
            ImPlot::SetNextLineStyle(QuadratureColor, 1.5f);
            ImPlot::PlotLine("Q", series.X.data(), series.Q.data(), static_cast<int>(series.X.size()));
            ImPlot::EndPlot();
        }
        ImPlot::EndSubplots();
    }
    m_PipelineFit = false;
}

void ViewsWindow::DrawSpectrumTab() {
    const auto &spectrum = m_Session.GetSpectrum();
    const auto &spectrum_db = m_Session.GetSpectrumDb();
    if (!ImGui::BeginTabItem("Spectrum")) {
        return;
    }
    int window = static_cast<int>(m_Session.GetSpectrumWindow());
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Window", &window, "Hann\0Hamming\0Blackman\0Rectangular\0")) {
        m_Session.SetSpectrumWindow(static_cast<Core::Window>(window));
    }
    if (!spectrum_db.empty()) {
        ImGui::SameLine();
        ExportImageButton("spectrum", [&] { return Core::SpectrumFigure(spectrum, spectrum_db); });
    }
    Hint("Hann is the default. Rectangular has the narrowest main lobe but the worst leakage; Blackman has the "
         "lowest sidelobes with a wider main lobe.");
    ImGui::Text("Two-sided Welch PSD | Periodic %s | %zu samples/segment | %zu segments",
                Core::WindowName(spectrum.Window), spectrum.SegmentLength, spectrum.SegmentCount);
    ImGui::TextWrapped("Relative power density; no impedance or watt/dBm calibration. Display floor: -200 dB.");
    if (spectrum_db.empty()) {
        ImGui::TextUnformatted("At least four samples are needed for a spectrum.");
    } else {
        if (m_SpectrumFit) {
            ImPlot::SetNextAxesToFit();
        }
        if (ImPlot::BeginPlot("Baseband PSD", ImVec2(-1, -1))) {
            m_SpectrumFit = false;
            ImPlot::SetupAxes("Frequency (Hz)", "PSD (dB re 1 amplitude^2/Hz)");
            ImPlot::SetNextLineStyle(InPhaseColor);
            ImPlot::PlotLine("I + jQ", spectrum.FrequencyHz.data(), spectrum_db.data(),
                             static_cast<int>(spectrum_db.size()));
            ImPlot::EndPlot();
        }
    }
    ImGui::EndTabItem();
}

/**
 * @brief   Draws an "Export image..." button that captures the plot and opens the image export dialog.
 * @param[in] stem   Plot name used in the suggested file name, siggen-<stem>.png or .svg.
 * @param[in] build  Builds the figure from the plotted data; called only when the button is pressed.
 */
void ViewsWindow::ExportImageButton(const char *stem, const std::function<Core::Figure()> &build) {
    if (ImGui::Button("Export image...")) {
        try {
            m_ImageFigure = build();
            std::snprintf(m_ImagePath, sizeof m_ImagePath, "siggen-%s.%s", stem, m_ImageFormat == 0 ? "png" : "svg");
            m_ImageStatus.clear();
            m_ImageConfirmOverwrite = false;
            m_ImageOpenRequested = true;
        } catch (const std::exception &error) {
            spdlog::error("Unable to prepare image: {}", error.what());
            m_Session.SetError(error.what());
        }
    }
    Hint("Save this plot as a PNG or SVG image for slides and reports. The image is drawn from the plotted data, so it "
         "does not depend on the window size.");
}

/**
 * @brief   Draws the image export dialog once ExportImageButton() requests it.
 * @note    The request is deferred to here because the buttons sit inside tab scopes, where opening the popup
 *          would give it an ID that this BeginPopupModal() call does not match.
 */
void ViewsWindow::DrawImageExportDialog() {
    if (m_ImageOpenRequested) {
        ImGui::OpenPopup("Export image");
        m_ImageOpenRequested = false;
    }
    ImGui::SetNextWindowSize(ImVec2(540, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Export image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped("Save the plot as an image. PNG is a raster for slides; SVG is vector and stays sharp at any "
                       "size in documents.");
    if (ImGui::InputText("Destination", m_ImagePath, sizeof m_ImagePath)) {
        m_ImageConfirmOverwrite = false;
    }
    if (ImGui::Combo("Format", &m_ImageFormat, "PNG\0SVG\0")) {
        // Keep the name, swap the extension
        auto path = std::filesystem::path(m_ImagePath);
        path.replace_extension(m_ImageFormat == 0 ? ".png" : ".svg");
        std::snprintf(m_ImagePath, sizeof m_ImagePath, "%s", path.string().c_str());
        m_ImageConfirmOverwrite = false;
    }
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Width (px)", &m_ImageWidth, 100, 400);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Height (px)", &m_ImageHeight, 100, 400);
    m_ImageWidth = std::clamp(m_ImageWidth, Core::MinImageSize, Core::MaxImageSize);
    m_ImageHeight = std::clamp(m_ImageHeight, Core::MinImageSize, Core::MaxImageSize);
    ImGui::Combo("Background", &m_ImageTheme, "Dark (matches the app)\0Light (for print)\0");
    if (ImGui::Button("Save image")) {
        try {
            m_ImageConfirmOverwrite = std::filesystem::exists(m_ImagePath);
            if (!m_ImageConfirmOverwrite) {
                ExportImage(false);
            }
        } catch (const std::exception &error) {
            m_ImageStatus = error.what();
        }
    }
    if (m_ImageConfirmOverwrite) {
        ImGui::TextWrapped("The destination exists. Replace it?");
        if (ImGui::Button("Confirm overwrite")) {
            ExportImage(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel overwrite")) {
            m_ImageConfirmOverwrite = false;
        }
    }
    if (!m_ImageStatus.empty()) {
        ImGui::TextWrapped("%s", m_ImageStatus.c_str());
    }
    if (ImGui::Button("Close")) {
        m_ImageFigure.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

/**
 * @brief   Writes the captured figure with the dialog settings, and records the outcome for the dialog.
 * @param[in] overwrite  True to replace an existing file.
 */
void ViewsWindow::ExportImage(bool overwrite) {
    try {
        Core::ImageStyle style;
        style.Width = m_ImageWidth;
        style.Height = m_ImageHeight;
        style.Dark = m_ImageTheme == 0;
        Core::ExportFigure(m_ImagePath, *m_ImageFigure,
                           m_ImageFormat == 0 ? Core::ImageFormat::PNG : Core::ImageFormat::SVG, style, overwrite);
        spdlog::info("Exported image {} ({}x{})", m_ImagePath, style.Width, style.Height);
        m_ImageStatus = std::string("Saved ") + m_ImagePath;
        m_ImageConfirmOverwrite = false;
    } catch (const std::exception &error) {
        spdlog::error("Image export to {} failed: {}", m_ImagePath, error.what());
        m_ImageStatus = std::string("Export failed: ") + error.what();
    }
}

} // namespace GUI
