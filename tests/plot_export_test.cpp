#include "generator.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_export.h"
#include "plot_figures.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace {
iq::Figure line_figure(iq::Rgb colour, std::string title = "Test figure") {
    iq::Panel p;
    p.x_label = "Time (s)";
    p.y_label = "Amplitude";
    iq::Series s;
    s.label = "I";
    s.color = colour;
    for (int i = 0; i <= 100; ++i) {
        s.x.push_back(i / 100.0);
        s.y.push_back(std::sin(6.28318 * i / 100.0));
    }
    p.series.push_back(s);
    return {std::move(title), {p}};
}
struct Decoded {
    int w = 0, h = 0;
    std::vector<unsigned char> rgb;
    const unsigned char* at(int x, int y) const { return &rgb[(static_cast<std::size_t>(y) * w + x) * 3]; }
};
Decoded decode(const std::vector<std::uint8_t>& png) {
    Decoded d;
    int n = 0;
    auto* p = stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &d.w, &d.h, &n, 3);
    EXPECT_NE(p, nullptr);
    if (p) d.rgb.assign(p, p + static_cast<std::size_t>(d.w) * d.h * 3);
    stbi_image_free(p);
    return d;
}
std::size_t count_near(const Decoded& d, iq::Rgb c, int tolerance = 40) {
    std::size_t n = 0;
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            const auto* p = d.at(x, y);
            if (std::abs(p[0] - c.r) <= tolerance && std::abs(p[1] - c.g) <= tolerance && std::abs(p[2] - c.b) <= tolerance) ++n;
        }
    return n;
}
std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / (std::string("siggen-plot-test-") + name);
}
}

TEST(PlotExport, NiceTicksAreRoundAndInsideRange) {
    const auto t = iq::nice_ticks(0, 1, 5);
    ASSERT_GE(t.size(), 4u);
    EXPECT_DOUBLE_EQ(t.front(), 0.0);
    EXPECT_NEAR(t[1], 0.2, 1e-12);
    for (double v : iq::nice_ticks(-3.2, 7.7, 6)) {
        EXPECT_GE(v, -3.2);
        EXPECT_LE(v, 7.7);
    }
    EXPECT_TRUE(iq::nice_ticks(1, 1).empty());
    EXPECT_TRUE(iq::nice_ticks(2, 1).empty());
}

TEST(PlotExport, TickLabels) {
    EXPECT_EQ(iq::format_tick(0, 0.5), "0");
    EXPECT_EQ(iq::format_tick(0.25, 0.25), "0.25");
    EXPECT_EQ(iq::format_tick(-2, 1), "-2");
    EXPECT_EQ(iq::format_tick(20000, 5000), "20000");
    EXPECT_EQ(iq::format_tick(2e-5, 1e-5), "2e-5");
    EXPECT_EQ(iq::format_tick(1.5e-4, 5e-5), "1.5e-4");
    EXPECT_EQ(iq::format_tick(3e6, 1e6), "3e6");
}

TEST(PlotExport, SvgIsWellFormedAndEscapesText) {
    auto fig = line_figure({255, 0, 0}, "A & B <tau>");
    const auto svg = iq::render_svg(fig, {800, 450, false});
    EXPECT_EQ(svg.rfind("<?xml", 0), 0u);
    EXPECT_NE(svg.find("width=\"800\" height=\"450\""), std::string::npos);
    EXPECT_NE(svg.find("A &amp; B &lt;tau&gt;"), std::string::npos);
    EXPECT_EQ(svg.find("<tau>"), std::string::npos);
    EXPECT_NE(svg.find("stroke=\"#ff0000\""), std::string::npos);
    EXPECT_NE(svg.find("Time (s)"), std::string::npos);
    EXPECT_NE(svg.find("rotate(-90"), std::string::npos); // the y label
    EXPECT_EQ(svg.substr(svg.size() - 7), "</svg>\n");
    // Every opened group is closed.
    std::size_t open = 0, close = 0;
    for (auto at = svg.find("<g "); at != std::string::npos; at = svg.find("<g ", at + 1)) ++open;
    for (auto at = svg.find("</g>"); at != std::string::npos; at = svg.find("</g>", at + 1)) ++close;
    EXPECT_EQ(open, close);
    const auto dark = iq::render_svg(fig, {800, 450, true});
    EXPECT_NE(dark.find("fill=\"#1b1f27\""), std::string::npos);
}

TEST(PlotExport, PngHasRequestedSizeBackgroundAndSeriesColour) {
    const auto png = iq::render_png(line_figure({255, 0, 0}), {640, 360, false});
    ASSERT_GT(png.size(), 8u);
    EXPECT_EQ(png[0], 0x89);
    EXPECT_EQ(png[1], 'P');
    const auto img = decode(png);
    EXPECT_EQ(img.w, 640);
    EXPECT_EQ(img.h, 360);
    EXPECT_EQ(img.at(2, 2)[0], 255); // white page
    EXPECT_EQ(img.at(2, 2)[2], 255);
    EXPECT_GT(count_near(img, {255, 0, 0}), 200u); // the curve
    EXPECT_GT(count_near(img, {32, 36, 44}, 60), 40u); // axes, ticks and text
    const auto dark = decode(iq::render_png(line_figure({255, 0, 0}), {640, 360, true}));
    EXPECT_LT(dark.at(2, 2)[0], 60);
}

TEST(PlotExport, SeriesOutsideAxisLimitsAreClipped) {
    auto fig = line_figure({255, 0, 0});
    fig.panels[0].x_limits = std::array<double, 2>{10, 11}; // data live in [0, 1]
    fig.panels[0].y_limits = std::array<double, 2>{-1, 1};
    fig.panels[0].series[0].label.clear();
    const auto img = decode(iq::render_png(fig, {640, 360}));
    EXPECT_EQ(count_near(img, {255, 0, 0}, 10), 0u);
}

TEST(PlotExport, EqualAspectKeepsASquareConstellationSquare) {
    iq::Panel p;
    p.equal_aspect = true;
    iq::Series s;
    s.kind = iq::Series::Kind::Scatter;
    s.color = {0, 0, 255};
    s.width = 8;
    s.x = {-1, 1, 1, -1};
    s.y = {-1, -1, 1, 1};
    p.series.push_back(s);
    const auto img = decode(iq::render_png({"", {p}}, {900, 450}));
    int x0 = img.w, x1 = 0, y0 = img.h, y1 = 0;
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x)
            if (img.at(x, y)[2] > 200 && img.at(x, y)[0] < 60) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    ASSERT_GT(x1, x0);
    EXPECT_NEAR(x1 - x0, y1 - y0, 4);
}

TEST(PlotExport, RejectsBadSizesAndEmptyFigures) {
    EXPECT_THROW(iq::render_svg(line_figure({0, 0, 0}), {100, 450}), std::invalid_argument);
    EXPECT_THROW(iq::render_png(line_figure({0, 0, 0}), {640, 100000}), std::invalid_argument);
    EXPECT_THROW(iq::render_svg({"empty", {}}, {}), std::invalid_argument);
    iq::Panel empty; // no series: still draws the frame
    EXPECT_NO_THROW(iq::render_png({"", {empty}}, {400, 300}));
}

TEST(PlotExport, HandlesNonFiniteAndDegenerateData) {
    iq::Panel p;
    iq::Series s;
    s.x = {0, 1, 2, 3};
    s.y = {1, NAN, 1, INFINITY};
    p.series.push_back(s);
    iq::Series flat;
    flat.x = {0, 1};
    flat.y = {5, 5};
    p.series.push_back(flat);
    EXPECT_NO_THROW(iq::render_png({"", {p}}, {400, 300}));
    EXPECT_NO_THROW(iq::render_svg({"", {p}}, {400, 300}));
}

TEST(PlotExport, ExportWritesFilesAndProtectsExistingOnes) {
    const auto png = temp_path("a.png"), svg = temp_path("a.svg");
    std::filesystem::remove(png);
    std::filesystem::remove(svg);
    const auto fig = line_figure({0, 128, 0});
    iq::export_figure(png, fig, iq::ImageFormat::PNG, {400, 300});
    iq::export_figure(svg, fig, iq::ImageFormat::SVG, {400, 300});
    EXPECT_GT(std::filesystem::file_size(png), 500u);
    std::ifstream in(svg);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_NE(text.str().find("<svg"), std::string::npos);
    EXPECT_THROW(iq::export_figure(png, fig, iq::ImageFormat::PNG, {400, 300}), std::runtime_error);
    EXPECT_NO_THROW(iq::export_figure(png, fig, iq::ImageFormat::PNG, {400, 300}, true));
    EXPECT_THROW(iq::export_figure("", fig, iq::ImageFormat::SVG), std::invalid_argument);
    EXPECT_THROW(iq::export_figure(temp_path("no-such-dir/x.png"), fig, iq::ImageFormat::PNG, {400, 300}), std::exception);
    std::filesystem::remove(png);
    std::filesystem::remove(svg);
}

TEST(PlotFigures, BuiltFromARealSignal) {
    iq::GenerationConfig config;
    config.modulation = iq::Modulation::QPSK;
    config.symbol_count = 64;
    config.awgn.enabled = true;
    config.awgn.snr_db = 20;
    const auto signal = iq::generate(config);
    const auto plots = make_plot_data(signal);
    const auto waveform = figures::waveform(plots);
    ASSERT_EQ(waveform.panels.size(), 1u);
    EXPECT_EQ(waveform.panels[0].series.size(), 2u);
    const auto constellation = figures::constellation(plots.matched, "I/Q constellation");
    EXPECT_TRUE(constellation.panels[0].equal_aspect);
    EXPECT_FALSE(constellation.panels[0].series[0].x.empty());
    const auto eye = figures::eye(iq::eye_diagram(signal), false);
    EXPECT_GT(eye.panels[0].series.size(), 10u);
    const auto spectrum = iq::welch_psd(signal.samples, signal.sample_rate_hz);
    EXPECT_EQ(figures::spectrum(spectrum, std::vector<double>(spectrum.frequency_hz.size(), -40)).panels[0].series.size(), 1u);
    const auto stages = iq::pipeline_stages(signal);
    const auto pipeline = figures::pipeline(stages, 4, 8, true);
    ASSERT_EQ(pipeline.panels.size(), 5u);
    EXPECT_EQ(pipeline.panels[4].x_label, "Time (symbol periods)");
    EXPECT_TRUE(pipeline.panels[0].x_label.empty());
    ASSERT_TRUE(pipeline.panels[3].x_limits);
    EXPECT_DOUBLE_EQ((*pipeline.panels[3].x_limits)[0], 4);
    EXPECT_DOUBLE_EQ((*pipeline.panels[3].x_limits)[1], 12);
    // Every figure renders to both formats.
    for (const auto* fig : {&waveform, &constellation, &eye, &pipeline}) {
        EXPECT_NO_THROW(iq::render_svg(*fig, {800, 600}));
        const auto img = decode(iq::render_png(*fig, {800, 600, false}));
        EXPECT_GT(count_near(img, {51, 153, 255}, 50), 8u);
    }
    // A request beyond the signal is clamped, not an error.
    EXPECT_NO_THROW(figures::pipeline(stages, 1000, 1000, false));
}

TEST(PlotExport, LogarithmicAxisDrawsDecadeTicksAndSkipsNonPositiveValues) {
    iq::Panel panel;
    panel.y_log = true;
    panel.x_label = "Eb/N0 (dB)";
    panel.y_label = "BER";
    iq::Series s;
    s.x = {0, 1, 2, 3};
    s.y = {0.1, 0.01, 0.0, 1e-4}; // The zero cannot be shown on a log axis.
    panel.series.push_back(s);
    const iq::Figure figure{"BER", {panel}};
    const auto svg = iq::render_svg(figure);
    EXPECT_NE(svg.find("1e-1"), std::string::npos);
    EXPECT_NE(svg.find("1e-4"), std::string::npos);
    EXPECT_EQ(svg.find("nan"), std::string::npos);
    panel.y_limits = std::array<double, 2>{1e-5, 1};
    EXPECT_NO_THROW(iq::render_png(iq::Figure{"BER", {panel}}));
}
