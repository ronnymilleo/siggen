#pragma once
#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include "plot_export.h"
#include <algorithm>
#include <cmath>
#include <string>

// Figures for image export, built from the same data the GUI plots. Colours match the screen:
// I blue, Q orange.
namespace figures {
inline constexpr iq::Rgb BLUE{51, 153, 255}, ORANGE{255, 140, 38};

inline iq::Series line(std::string label, const std::vector<double>& x, const std::vector<double>& y, iq::Rgb colour,
                       double width = 2.0, double alpha = 1.0)
{
    iq::Series s;
    s.label = std::move(label);
    s.x     = x;
    s.y     = y;
    s.color = colour;
    s.width = width;
    s.alpha = alpha;
    return s;
}
inline iq::Figure waveform(const PlotData& plot)
{
    iq::Panel p;
    p.title = "";
    p.x_label = "Time (s)";
    p.y_label = "Amplitude";
    p.zero_line = true;
    p.series.push_back(line("I", plot.time, plot.i, BLUE));
    p.series.push_back(line("Q", plot.time, plot.q, ORANGE));
    return {"Complex baseband", {p}};
}
inline iq::Figure frequency(const PlotData& plot)
{
    iq::Panel p;
    p.x_label = "Time (s)";
    p.y_label = "Frequency (Hz)";
    p.series.push_back(line("Estimate", plot.freq_time, plot.freq_estimate, BLUE));
    p.series.push_back(line("Nominal tone", plot.freq_time, plot.freq_nominal, ORANGE, 2.5));
    return {"Instantaneous frequency", {p}};
}
inline iq::Figure constellation(const XYData& data, std::string title)
{
    iq::Panel p;
    p.x_label = "In-phase (I)";
    p.y_label = "Quadrature (Q)";
    p.equal_aspect = true;
    p.zero_line = true;
    iq::Series s = line("", data.x, data.y, BLUE, 4.0);
    s.kind = iq::Series::Kind::Scatter;
    p.series.push_back(std::move(s));
    return {std::move(title), {p}};
}
inline iq::Figure eye(const iq::EyeDiagram& eye, bool quadrature)
{
    iq::Panel p;
    p.x_label = "Time (symbol periods)";
    p.y_label = quadrature ? "Q" : "I";
    for (const auto& trace : quadrature ? eye.quadrature : eye.in_phase)
        p.series.push_back(line("", eye.time_symbols, trace, quadrature ? ORANGE : BLUE, 1.6, .3));
    return {quadrature ? "Eye diagram (quadrature)" : "Eye diagram (in-phase)", {p}};
}
inline iq::Figure spectrum(const iq::Spectrum& spectrum, const std::vector<double>& db)
{
    iq::Panel p;
    p.x_label = "Frequency (Hz)";
    p.y_label = "PSD (dB re 1 amplitude^2/Hz)";
    p.series.push_back(line("I + jQ", spectrum.frequency_hz, db, BLUE));
    return {std::string("Baseband PSD (") + iq::window_name(spectrum.window) + " window)", {p}};
}
// The five pipeline rows for symbols [first, first + count), x in symbol periods.
inline iq::Figure pipeline(const iq::PipelineStages& st, int first, int count, bool align)
{
    const auto total = static_cast<int>(st.symbols.size());
    count            = std::clamp(count, 1, std::max(total, 1));
    first            = std::clamp(first, 0, std::max(total - count, 0));
    const int sps    = st.samples_per_symbol, bps = std::max(st.bits_per_symbol, 1);
    const auto shift = align ? st.filter_delay_samples : 0;
    double     peak  = 1e-9;
    for (const auto* stage : {&st.symbols, &st.upsampled, &st.shaped, &st.received})
        for (const auto& v : *stage)
            peak = std::max({peak, static_cast<double>(std::abs(v.real())), static_cast<double>(std::abs(v.imag()))});
    const std::array<double, 2> yrange{-peak * 1.25, peak * 1.25};
    auto samples = [&](const std::vector<std::complex<float>>& stage, std::size_t extra, bool closed, std::vector<double>& x,
                       std::vector<double>& i, std::vector<double>& q) {
        const auto begin = static_cast<std::size_t>(first) * static_cast<std::size_t>(sps) + extra;
        const auto n     = static_cast<std::size_t>(count) * static_cast<std::size_t>(sps) + (closed ? 1 : 0);
        for (std::size_t k = 0; k < n && begin + k < stage.size(); ++k)
        {
            x.push_back(first + static_cast<double>(k) / sps);
            i.push_back(stage[begin + k].real());
            q.push_back(stage[begin + k].imag());
        }
    };
    auto panel = [&](std::string title, bool last) {
        iq::Panel p;
        p.title       = std::move(title);
        p.x_limits    = std::array<double, 2>{static_cast<double>(first), static_cast<double>(first + count)};
        p.y_limits    = yrange;
        p.zero_line   = true;
        if (last)
            p.x_label = "Time (symbol periods)";
        return p;
    };
    iq::Figure fig;
    fig.title = "Pipeline: from data bits to transmitted samples";
    {
        auto p = panel("1. Data bits", false);
        p.y_limits = std::array<double, 2>{-.3, 1.3};
        std::vector<double> x, y;
        const int           b0 = first * bps, b1 = std::min(static_cast<int>(st.bits.size()), (first + count) * bps);
        for (int b = b0; b < b1; ++b)
        {
            x.push_back(static_cast<double>(b) / bps);
            y.push_back(st.bits[static_cast<std::size_t>(b)] == '1' ? 1. : 0.);
        }
        if (!x.empty())
        {
            x.push_back(static_cast<double>(b1) / bps);
            y.push_back(y.back());
        }
        auto s = line("", x, y, BLUE);
        s.kind = iq::Series::Kind::Stairs;
        p.series.push_back(std::move(s));
        fig.panels.push_back(std::move(p));
    }
    {
        auto p = panel("2. Mapped symbols (one complex value per symbol)", false);
        std::vector<double> x, yi, yq;
        for (int k = first; k < first + count && k < total; ++k)
        {
            x.push_back(k);
            yi.push_back(st.symbols[static_cast<std::size_t>(k)].real());
            yq.push_back(st.symbols[static_cast<std::size_t>(k)].imag());
        }
        if (!x.empty())
        {
            x.push_back(first + count);
            yi.push_back(yi.back());
            yq.push_back(yq.back());
        }
        auto si = line("I", x, yi, BLUE), sq = line("Q", x, yq, ORANGE);
        si.kind = sq.kind = iq::Series::Kind::Stairs;
        p.series.push_back(std::move(si));
        p.series.push_back(std::move(sq));
        fig.panels.push_back(std::move(p));
    }
    {
        auto p = panel("3. Zeros inserted between symbols (upsampling by SPS)", false);
        std::vector<double> x, i, q;
        samples(st.upsampled, 0, false, x, i, q);
        auto si = line("I", x, i, BLUE, 1.6), sq = line("Q", x, q, ORANGE, 1.6);
        si.kind = sq.kind = iq::Series::Kind::Stems;
        p.series.push_back(std::move(si));
        p.series.push_back(std::move(sq));
        fig.panels.push_back(std::move(p));
    }
    {
        auto p = panel("4. After the pulse filter", false);
        std::vector<double> x, i, q;
        samples(st.shaped, shift, true, x, i, q);
        p.series.push_back(line("I", x, i, BLUE, 1.8));
        p.series.push_back(line("Q", x, q, ORANGE, 1.8));
        fig.panels.push_back(std::move(p));
    }
    {
        auto p = panel(st.degraded ? "5. With noise and impairments (the exported signal)" : "5. With noise: none added", true);
        std::vector<double> x, i, q;
        samples(st.received, shift, true, x, i, q);
        p.series.push_back(line("I", x, i, BLUE, 1.8));
        p.series.push_back(line("Q", x, q, ORANGE, 1.8));
        fig.panels.push_back(std::move(p));
    }
    return fig;
}
} // namespace figures
