/**
 * @file    plot_figures.cpp
 * @brief   Figures for image export, built from the same data the GUI plots.
 */

#include "plot_figures.h"

#include "theory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>

namespace Core {

namespace {

/**
 * @struct  PipelineView
 * @brief   The symbol range a pipeline figure shows and the axis settings its panels share.
 */
struct PipelineView {
    int First = 0;
    int Count = 1;
    int SymbolCount = 0;
    int SamplesPerSymbol = 0;
    int BitsPerSymbol = 1;
    std::size_t Shift = 0;          // Filter delay skipped in the shaped and received stages when aligned
    std::array<double, 2> YRange{}; // Shared by every panel except the bits
};

/**
 * @struct  StageSamples
 * @brief   Samples of one pipeline stage inside the view, with x in symbol periods.
 */
struct StageSamples {
    std::vector<double> X, I, Q;
};

double PipelinePeak(const Core::PipelineStages &stages) {
    double peak = 1e-9;
    for (const auto *stage : {&stages.Symbols, &stages.Upsampled, &stages.Shaped, &stages.Received}) {
        for (const auto &value : *stage) {
            peak = std::max(
                {peak, static_cast<double>(std::abs(value.real())), static_cast<double>(std::abs(value.imag()))});
        }
    }
    return peak;
}

PipelineView MakePipelineView(const Core::PipelineStages &stages, int first, int count, const bool align) {
    PipelineView view;
    view.SymbolCount = static_cast<int>(stages.Symbols.size());
    count = std::clamp(count, 1, std::max(view.SymbolCount, 1));
    first = std::clamp(first, 0, std::max(view.SymbolCount - count, 0));
    view.First = first;
    view.Count = count;
    view.SamplesPerSymbol = stages.SamplesPerSymbol;
    view.BitsPerSymbol = std::max(stages.BitsPerSymbol, 1);
    view.Shift = align ? stages.FilterDelaySamples : 0;
    const double peak = PipelinePeak(stages);
    view.YRange = {-peak * 1.25, peak * 1.25};
    return view;
}

/**
 * @brief   Takes the samples of a stage that fall inside the view.
 * @param[in] stage     Stage samples, SamplesPerSymbol per symbol.
 * @param[in] view      Symbol range shown.
 * @param[in] skip      Samples to skip at the start of the stage (filter delay when aligned).
 * @param[in] closed    Adds the first sample after the range, so a continuous line reaches the right edge.
 * @return  The samples, cut short when the stage ends first.
 */
StageSamples TakeStageSamples(const std::vector<std::complex<float>> &stage, const PipelineView &view,
                              const std::size_t skip, const bool closed) {
    StageSamples samples;
    const auto begin = static_cast<std::size_t>(view.First) * static_cast<std::size_t>(view.SamplesPerSymbol) + skip;
    const auto length =
        static_cast<std::size_t>(view.Count) * static_cast<std::size_t>(view.SamplesPerSymbol) + (closed ? 1 : 0);
    for (std::size_t k = 0; k < length && begin + k < stage.size(); ++k) {
        samples.X.push_back(view.First + static_cast<double>(k) / view.SamplesPerSymbol);
        samples.I.push_back(stage[begin + k].real());
        samples.Q.push_back(stage[begin + k].imag());
    }
    return samples;
}

Core::Panel MakePipelinePanel(const PipelineView &view, std::string title, const bool last) {
    Core::Panel panel;
    panel.Title = std::move(title);
    panel.XLimits =
        std::array<double, 2>{static_cast<double>(view.First), static_cast<double>(view.First + view.Count)};
    panel.YLimits = view.YRange;
    panel.ZeroLine = true;
    if (last) {
        panel.XLabel = "Time (symbol periods)";
    }
    return panel;
}

Core::Panel BitsPanel(const Core::PipelineStages &stages, const PipelineView &view) {
    auto panel = MakePipelinePanel(view, "1. Data bits", false);
    panel.YLimits = std::array<double, 2>{-.3, 1.3};
    std::vector<double> x, y;
    const int first_bit = view.First * view.BitsPerSymbol;
    const int end_bit = std::min(static_cast<int>(stages.Bits.size()), (view.First + view.Count) * view.BitsPerSymbol);
    for (int bit = first_bit; bit < end_bit; ++bit) {
        x.push_back(static_cast<double>(bit) / view.BitsPerSymbol);
        y.push_back(stages.Bits[static_cast<std::size_t>(bit)] == '1' ? 1. : 0.);
    }
    // Repeat the last level so the final step has its full width
    if (!x.empty()) {
        x.push_back(static_cast<double>(end_bit) / view.BitsPerSymbol);
        y.push_back(y.back());
    }
    auto series = MakeLineSeries("", x, y, InPhaseColor);
    series.Kind = Core::SeriesKind::Stairs;
    panel.Series.push_back(std::move(series));
    return panel;
}

Core::Panel SymbolsPanel(const Core::PipelineStages &stages, const PipelineView &view) {
    auto panel = MakePipelinePanel(view, "2. Mapped symbols (one complex value per symbol)", false);
    std::vector<double> x, in_phase, quadrature;
    for (int k = view.First; k < view.First + view.Count && k < view.SymbolCount; ++k) {
        x.push_back(k);
        in_phase.push_back(stages.Symbols[static_cast<std::size_t>(k)].real());
        quadrature.push_back(stages.Symbols[static_cast<std::size_t>(k)].imag());
    }
    if (!x.empty()) {
        x.push_back(view.First + view.Count);
        in_phase.push_back(in_phase.back());
        quadrature.push_back(quadrature.back());
    }
    auto in_phase_series = MakeLineSeries("I", x, in_phase, InPhaseColor);
    auto quadrature_series = MakeLineSeries("Q", x, quadrature, QuadratureColor);
    in_phase_series.Kind = quadrature_series.Kind = Core::SeriesKind::Stairs;
    panel.Series.push_back(std::move(in_phase_series));
    panel.Series.push_back(std::move(quadrature_series));
    return panel;
}

Core::Panel UpsampledPanel(const Core::PipelineStages &stages, const PipelineView &view) {
    auto panel = MakePipelinePanel(view, "3. Zeros inserted between symbols (upsampling by SPS)", false);
    const auto samples = TakeStageSamples(stages.Upsampled, view, 0, false);
    auto in_phase_series = MakeLineSeries("I", samples.X, samples.I, InPhaseColor, 1.6);
    auto quadrature_series = MakeLineSeries("Q", samples.X, samples.Q, QuadratureColor, 1.6);
    in_phase_series.Kind = quadrature_series.Kind = Core::SeriesKind::Stems;
    panel.Series.push_back(std::move(in_phase_series));
    panel.Series.push_back(std::move(quadrature_series));
    return panel;
}

Core::Panel ShapedPanel(const Core::PipelineStages &stages, const PipelineView &view) {
    auto panel = MakePipelinePanel(view, "4. After the pulse filter", false);
    const auto samples = TakeStageSamples(stages.Shaped, view, view.Shift, true);
    panel.Series.push_back(MakeLineSeries("I", samples.X, samples.I, InPhaseColor, 1.8));
    panel.Series.push_back(MakeLineSeries("Q", samples.X, samples.Q, QuadratureColor, 1.8));
    return panel;
}

Core::Panel ReceivedPanel(const Core::PipelineStages &stages, const PipelineView &view) {
    auto panel = MakePipelinePanel(
        view, stages.Degraded ? "5. With noise and impairments (the exported signal)" : "5. With noise: none added",
        true);
    const auto samples = TakeStageSamples(stages.Received, view, view.Shift, true);
    panel.Series.push_back(MakeLineSeries("I", samples.X, samples.I, InPhaseColor, 1.8));
    panel.Series.push_back(MakeLineSeries("Q", samples.X, samples.Q, QuadratureColor, 1.8));
    return panel;
}

} // namespace

/**
 * @brief   Builds a line series from copies of the given coordinates.
 * @param[in] label     Legend label; empty for no legend entry.
 * @param[in] x         X coordinates.
 * @param[in] y         Y coordinates, same length as @p x.
 * @param[in] colour    Line colour.
 * @param[in] width     Line width in pixels at 1600x900.
 * @param[in] alpha     Opacity, 0 to 1.
 * @return  The series, of kind Line.
 */
Core::Series MakeLineSeries(std::string label, const std::vector<double> &x, const std::vector<double> &y,
                            const Core::Rgb colour, const double width, const double alpha) {
    Core::Series series;
    series.Label = std::move(label);
    series.X = x;
    series.Y = y;
    series.Color = colour;
    series.Width = width;
    series.Alpha = alpha;
    return series;
}

/**
 * @brief   Builds the complex baseband figure: I and Q against time.
 * @param[in] plot  Plot data of the signal.
 * @return  A one-panel figure.
 */
Core::Figure WaveformFigure(const PlotData &plot) {
    Core::Panel panel;
    panel.Title = "";
    panel.XLabel = "Time (s)";
    panel.YLabel = "Amplitude";
    panel.ZeroLine = true;
    panel.Series.push_back(MakeLineSeries("I", plot.Time, plot.I, InPhaseColor));
    panel.Series.push_back(MakeLineSeries("Q", plot.Time, plot.Q, QuadratureColor));
    return {"Complex baseband", {panel}};
}

/**
 * @brief   Builds the instantaneous frequency figure of an FSK signal: estimate against the nominal tone.
 * @param[in] plot  Plot data of the signal; its frequency track is empty for other families.
 * @return  A one-panel figure.
 */
Core::Figure FrequencyFigure(const PlotData &plot) {
    Core::Panel panel;
    panel.XLabel = "Time (s)";
    panel.YLabel = "Frequency (Hz)";
    panel.Series.push_back(MakeLineSeries("Estimate", plot.FreqTime, plot.FreqEstimate, InPhaseColor));
    panel.Series.push_back(MakeLineSeries("Nominal tone", plot.FreqTime, plot.FreqNominal, QuadratureColor, 2.5));
    return {"Instantaneous frequency", {panel}};
}

/**
 * @brief   Builds a constellation scatter with equal scales on both axes.
 * @param[in] data  I and Q coordinates of the points.
 * @param[in] title Figure title.
 * @return  A one-panel figure.
 */
Core::Figure ConstellationFigure(const XYData &data, std::string title) {
    Core::Panel panel;
    panel.XLabel = "In-phase (I)";
    panel.YLabel = "Quadrature (Q)";
    panel.EqualAspect = true;
    panel.ZeroLine = true;
    Core::Series series = MakeLineSeries("", data.X, data.Y, InPhaseColor, 4.0);
    series.Kind = Core::SeriesKind::Scatter;
    panel.Series.push_back(std::move(series));
    return {std::move(title), {panel}};
}

/**
 * @brief   Builds an eye diagram figure of one component, one translucent trace per symbol.
 * @param[in] eye           Eye diagram traces.
 * @param[in] quadrature    True for the Q component, false for I.
 * @return  A one-panel figure.
 */
Core::Figure EyeFigure(const Core::EyeDiagram &eye, const bool quadrature) {
    Core::Panel panel;
    panel.XLabel = "Time (symbol periods)";
    panel.YLabel = quadrature ? "Q" : "I";
    for (const auto &trace : quadrature ? eye.Quadrature : eye.InPhase) {
        panel.Series.push_back(
            MakeLineSeries("", eye.TimeSymbols, trace, quadrature ? QuadratureColor : InPhaseColor, 1.6, .3));
    }
    return {quadrature ? "Eye diagram (quadrature)" : "Eye diagram (in-phase)", {panel}};
}

/**
 * @brief   Builds the baseband power spectral density figure.
 * @param[in] spectrum  Spectrum estimate; gives the frequencies and the window name.
 * @param[in] db        Density in dB at each frequency of @p spectrum.
 * @return  A one-panel figure.
 */
Core::Figure SpectrumFigure(const Core::Spectrum &spectrum, const std::vector<double> &db) {
    Core::Panel panel;
    panel.XLabel = "Frequency (Hz)";
    panel.YLabel = "PSD (dB re 1 amplitude^2/Hz)";
    panel.Series.push_back(MakeLineSeries("I + jQ", spectrum.FrequencyHz, db, InPhaseColor));
    return {std::string("Baseband PSD (") + Core::WindowName(spectrum.Window) + " window)", {panel}};
}

/**
 * @brief   Builds the five pipeline rows (bits, symbols, upsampled, shaped, received) for a range of symbols.
 * @param[in] stages    Pipeline stages of a linear signal.
 * @param[in] first     First symbol shown; clamped so the range stays inside the signal.
 * @param[in] count     Number of symbols shown; clamped to [1, symbol count].
 * @param[in] align     Skips the pulse filter delay in the shaped and received rows so they line up with the symbols.
 * @return  A five-panel figure with x in symbol periods and a shared y range (except the bits).
 */
Core::Figure PipelineFigure(const Core::PipelineStages &stages, const int first, const int count, const bool align) {
    const auto view = MakePipelineView(stages, first, count, align);
    Core::Figure figure;
    figure.Title = "Pipeline: from data bits to transmitted samples";
    figure.Panels.push_back(BitsPanel(stages, view));
    figure.Panels.push_back(SymbolsPanel(stages, view));
    figure.Panels.push_back(UpsampledPanel(stages, view));
    figure.Panels.push_back(ShapedPanel(stages, view));
    figure.Panels.push_back(ReceivedPanel(stages, view));
    return figure;
}

/**
 * @brief   Splits a BER sweep for plotting.
 * @param[in] points      The measured points.
 * @param[in] modulation  Waveform of the sweep, for the textbook curve.
 * @return  The measured points with errors, the error-free points drawn at their 1 / bits upper bound, and the
 *          textbook curve sampled at 121 points over the swept range. The curve stops where the waveform has no
 *          closed form or where it drops below what the measurement could resolve.
 */
BerPlot MakeBerPlot(const std::vector<BerPoint> &points, const Modulation modulation) {
    BerPlot plot;
    double lo = INFINITY, hi = -INFINITY;
    const double floor_ber = LowestResolvableBer(points);
    for (const auto &point : points) {
        lo = std::min(lo, point.EbN0Db);
        hi = std::max(hi, point.EbN0Db);
        if (point.Errors.BitErrorCount > 0) {
            plot.X.push_back(point.EbN0Db);
            plot.Y.push_back(point.Ber());
        } else if (point.Errors.BitCount > 0) {
            plot.BoundX.push_back(point.EbN0Db);
            plot.BoundY.push_back(1.0 / static_cast<double>(point.Errors.BitCount));
        }
    }
    if (points.empty() || !(hi > lo)) {
        return plot;
    }
    for (int k = 0; k <= 120; ++k) {
        const double eb_n0_db = lo + (hi - lo) * k / 120.0;
        const auto ber = TheoreticalBer(modulation, eb_n0_db);
        // Below what the measurement could resolve: leave the axis alone
        if (!ber || *ber < floor_ber * 0.3) {
            break;
        }
        plot.TheoryX.push_back(eb_n0_db);
        plot.TheoryY.push_back(*ber);
    }
    return plot;
}

/**
 * @brief   Returns the smallest BER the sweep could resolve: one error in the most bits compared at any point.
 * @param[in] points  The measured points.
 * @return  1 / (most bits compared), or 1 when no point compared any bit.
 */
double LowestResolvableBer(const std::vector<BerPoint> &points) {
    double floor_ber = 1;
    for (const auto &point : points) {
        if (point.Errors.BitCount > 0) {
            floor_ber = std::min(floor_ber, 1.0 / static_cast<double>(point.Errors.BitCount));
        }
    }
    return floor_ber;
}

/**
 * @brief   Builds the BER-against-Eb/N0 figure, on a logarithmic BER axis down to the resolvable decade.
 * @param[in] points      The measured points.
 * @param[in] modulation  Waveform of the sweep.
 * @param[in] current     Optional Eb/N0 (dB) and BER of the displayed signal, marked when its BER is positive.
 * @return  The figure: textbook curve, measured line and markers, error-free upper bounds and the current signal.
 */
Core::Figure BerFigure(const std::vector<BerPoint> &points, const Modulation modulation,
                       const std::optional<std::array<double, 2>> current) {
    const auto plot = MakeBerPlot(points, modulation);
    Core::Panel panel;
    panel.XLabel = "Eb/N0 (dB)";
    panel.YLabel = "Bit error rate";
    panel.YLog = true;
    const double floor_ber = LowestResolvableBer(points);
    panel.YLimits = std::array<double, 2>{std::pow(10.0, std::floor(std::log10(std::max(floor_ber, 1e-12)))), 1.0};
    if (!plot.TheoryX.empty()) {
        panel.Series.push_back(
            MakeLineSeries("Theory (ideal receiver)", plot.TheoryX, plot.TheoryY, QuadratureColor, 2.0));
    }
    auto measured = MakeLineSeries("Measured", plot.X, plot.Y, InPhaseColor, 2.0);
    panel.Series.push_back(measured);
    measured.Kind = SeriesKind::Scatter;
    measured.Label = "";
    measured.Width = 5.0;
    panel.Series.push_back(measured);
    if (!plot.BoundX.empty()) {
        auto bound =
            MakeLineSeries("No errors seen (BER below this)", plot.BoundX, plot.BoundY, Core::Rgb{139, 148, 163}, 5.0);
        bound.Kind = SeriesKind::Scatter;
        panel.Series.push_back(bound);
    }
    if (current && (*current)[1] > 0) {
        auto now = MakeLineSeries("Displayed signal", {(*current)[0]}, {(*current)[1]}, Core::Rgb{110, 220, 140}, 8.0);
        now.Kind = SeriesKind::Scatter;
        panel.Series.push_back(now);
    }
    return {std::string("BER against Eb/N0: ") + ModulationName(modulation), {panel}};
}

} // namespace Core
