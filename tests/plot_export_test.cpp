/**
 * @file    plot_export_test.cpp
 * @brief   Tests for figure rendering to SVG and PNG, tick generation and the figures built from signals.
 */

#include "plot_export.h"

#include "generator.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_figures.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Core {

namespace {

Figure LineFigure(Rgb colour, std::string title = "Test figure") {
    Panel panel;
    panel.XLabel = "Time (s)";
    panel.YLabel = "Amplitude";
    Series series;
    series.Label = "I";
    series.Color = colour;
    for (int i = 0; i <= 100; ++i) {
        series.X.push_back(i / 100.0);
        series.Y.push_back(std::sin(6.28318 * i / 100.0));
    }
    panel.Series.push_back(series);
    return {std::move(title), {panel}};
}

// PNG decoded to 8-bit RGB, row by row
struct Decoded {
    int Width = 0, Height = 0;
    std::vector<unsigned char> Pixels;
    const unsigned char *At(int x, int y) const { return &Pixels[(static_cast<std::size_t>(y) * Width + x) * 3]; }
};

Decoded Decode(const std::vector<std::uint8_t> &png) {
    Decoded image;
    int channels = 0;
    auto *pixels =
        stbi_load_from_memory(png.data(), static_cast<int>(png.size()), &image.Width, &image.Height, &channels, 3);
    EXPECT_NE(pixels, nullptr);
    if (pixels) {
        image.Pixels.assign(pixels, pixels + static_cast<std::size_t>(image.Width) * image.Height * 3);
    }
    stbi_image_free(pixels);
    return image;
}

// Dark ink on a light page: every channel below 128, which leaves out the light background, the grid and saturated
// series colours. Unlike a match on one colour, the count barely depends on which font draws the text
std::size_t CountInk(const Decoded &image) {
    std::size_t count = 0;
    for (int y = 0; y < image.Height; ++y) {
        for (int x = 0; x < image.Width; ++x) {
            const auto *pixel = image.At(x, y);
            if (pixel[0] < 128 && pixel[1] < 128 && pixel[2] < 128) {
                ++count;
            }
        }
    }
    return count;
}

std::size_t CountNear(const Decoded &image, Rgb colour, int tolerance = 40) {
    std::size_t count = 0;
    for (int y = 0; y < image.Height; ++y) {
        for (int x = 0; x < image.Width; ++x) {
            const auto *pixel = image.At(x, y);
            if (std::abs(pixel[0] - colour.R) <= tolerance && std::abs(pixel[1] - colour.G) <= tolerance &&
                std::abs(pixel[2] - colour.B) <= tolerance) {
                ++count;
            }
        }
    }
    return count;
}

std::filesystem::path TempPath(const char *name) {
    return std::filesystem::temp_directory_path() / (std::string("siggen-plot-test-") + name);
}

} // namespace

TEST(PlotExport, NiceTicksAreRoundAndInsideRange) {
    const auto ticks = NiceTicks(0, 1, 5);
    ASSERT_GE(ticks.size(), 4u);
    EXPECT_DOUBLE_EQ(ticks.front(), 0.0);
    EXPECT_NEAR(ticks[1], 0.2, 1e-12);
    for (double tick : NiceTicks(-3.2, 7.7, 6)) {
        EXPECT_GE(tick, -3.2);
        EXPECT_LE(tick, 7.7);
    }
    EXPECT_TRUE(NiceTicks(1, 1).empty());
    EXPECT_TRUE(NiceTicks(2, 1).empty());
}

TEST(PlotExport, TickLabels) {
    EXPECT_EQ(FormatTick(0, 0.5), "0");
    EXPECT_EQ(FormatTick(0.25, 0.25), "0.25");
    EXPECT_EQ(FormatTick(-2, 1), "-2");
    EXPECT_EQ(FormatTick(20000, 5000), "20000");
    EXPECT_EQ(FormatTick(2e-5, 1e-5), "2e-5");
    EXPECT_EQ(FormatTick(1.5e-4, 5e-5), "1.5e-4");
    EXPECT_EQ(FormatTick(3e6, 1e6), "3e6");
}

TEST(PlotExport, SvgIsWellFormedAndEscapesText) {
    auto figure = LineFigure({255, 0, 0}, "A & B <tau>");
    const auto svg = RenderSvg(figure, {800, 450, false});
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
    for (auto at = svg.find("<g "); at != std::string::npos; at = svg.find("<g ", at + 1)) {
        ++open;
    }
    for (auto at = svg.find("</g>"); at != std::string::npos; at = svg.find("</g>", at + 1)) {
        ++close;
    }
    EXPECT_EQ(open, close);
    const auto dark = RenderSvg(figure, {800, 450, true});
    EXPECT_NE(dark.find("fill=\"#1b1f27\""), std::string::npos);
}

TEST(PlotExport, PngHasRequestedSizeBackgroundAndSeriesColour) {
    const auto png = RenderPng(LineFigure({255, 0, 0}), {640, 360, false});
    ASSERT_GT(png.size(), 8u);
    EXPECT_EQ(png[0], 0x89);
    EXPECT_EQ(png[1], 'P');
    const auto image = Decode(png);
    EXPECT_EQ(image.Width, 640);
    EXPECT_EQ(image.Height, 360);
    EXPECT_EQ(image.At(2, 2)[0], 255); // white page
    EXPECT_EQ(image.At(2, 2)[2], 255);
    EXPECT_GT(CountNear(image, {255, 0, 0}), 200u); // the curve
    EXPECT_GT(CountInk(image), 40u);                // axes, ticks and text
    const auto dark = Decode(RenderPng(LineFigure({255, 0, 0}), {640, 360, true}));
    EXPECT_LT(dark.At(2, 2)[0], 60);
}

TEST(PlotExport, SeriesOutsideAxisLimitsAreClipped) {
    auto figure = LineFigure({255, 0, 0});
    figure.Panels[0].XLimits = std::array<double, 2>{10, 11}; // data live in [0, 1]
    figure.Panels[0].YLimits = std::array<double, 2>{-1, 1};
    figure.Panels[0].Series[0].Label.clear();
    const auto image = Decode(RenderPng(figure, {640, 360}));
    EXPECT_EQ(CountNear(image, {255, 0, 0}, 10), 0u);
}

TEST(PlotExport, EqualAspectKeepsASquareConstellationSquare) {
    Panel panel;
    panel.EqualAspect = true;
    Series series;
    series.Kind = SeriesKind::Scatter;
    series.Color = {0, 0, 255};
    series.Width = 8;
    series.X = {-1, 1, 1, -1};
    series.Y = {-1, -1, 1, 1};
    panel.Series.push_back(series);
    const auto image = Decode(RenderPng({"", {panel}}, {900, 450}));
    int x0 = image.Width, x1 = 0, y0 = image.Height, y1 = 0;
    for (int y = 0; y < image.Height; ++y) {
        for (int x = 0; x < image.Width; ++x) {
            if (image.At(x, y)[2] > 200 && image.At(x, y)[0] < 60) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    ASSERT_GT(x1, x0);
    EXPECT_NEAR(x1 - x0, y1 - y0, 4);
}

TEST(PlotExport, RejectsBadSizesAndEmptyFigures) {
    EXPECT_THROW(RenderSvg(LineFigure({0, 0, 0}), {100, 450}), std::invalid_argument);
    EXPECT_THROW(RenderPng(LineFigure({0, 0, 0}), {640, 100000}), std::invalid_argument);
    EXPECT_THROW(RenderSvg({"empty", {}}, {}), std::invalid_argument);
    Panel empty; // no series: still draws the frame
    EXPECT_NO_THROW(RenderPng({"", {empty}}, {400, 300}));
}

TEST(PlotExport, HandlesNonFiniteAndDegenerateData) {
    Panel panel;
    Series series;
    series.X = {0, 1, 2, 3};
    series.Y = {1, NAN, 1, INFINITY};
    panel.Series.push_back(series);
    Series flat;
    flat.X = {0, 1};
    flat.Y = {5, 5};
    panel.Series.push_back(flat);
    EXPECT_NO_THROW(RenderPng({"", {panel}}, {400, 300}));
    EXPECT_NO_THROW(RenderSvg({"", {panel}}, {400, 300}));
}

TEST(PlotExport, ExportWritesFilesAndProtectsExistingOnes) {
    const auto png = TempPath("a.png"), svg = TempPath("a.svg");
    std::filesystem::remove(png);
    std::filesystem::remove(svg);
    const auto figure = LineFigure({0, 128, 0});
    ExportFigure(png, figure, ImageFormat::PNG, {400, 300});
    ExportFigure(svg, figure, ImageFormat::SVG, {400, 300});
    EXPECT_GT(std::filesystem::file_size(png), 500u);
    std::ifstream in(svg);
    std::stringstream text;
    text << in.rdbuf();
    EXPECT_NE(text.str().find("<svg"), std::string::npos);
    EXPECT_THROW(ExportFigure(png, figure, ImageFormat::PNG, {400, 300}), std::runtime_error);
    EXPECT_NO_THROW(ExportFigure(png, figure, ImageFormat::PNG, {400, 300}, true));
    EXPECT_THROW(ExportFigure("", figure, ImageFormat::SVG), std::invalid_argument);
    EXPECT_THROW(ExportFigure(TempPath("no-such-dir/x.png"), figure, ImageFormat::PNG, {400, 300}), std::exception);
    std::filesystem::remove(png);
    std::filesystem::remove(svg);
}

TEST(PlotFigures, BuiltFromARealSignal) {
    GenerationConfig config;
    config.Modulation = Modulation::QPSK;
    config.SymbolCount = 64;
    config.Awgn.Enabled = true;
    config.Awgn.SnrDb = 20;
    const auto signal = Generate(config);
    const auto plots = MakePlotData(signal);
    const auto waveform = WaveformFigure(plots);
    ASSERT_EQ(waveform.Panels.size(), 1u);
    EXPECT_EQ(waveform.Panels[0].Series.size(), 2u);
    const auto constellation = ConstellationFigure(plots.Matched, "I/Q constellation");
    EXPECT_TRUE(constellation.Panels[0].EqualAspect);
    EXPECT_FALSE(constellation.Panels[0].Series[0].X.empty());
    const auto eye = EyeFigure(BuildEyeDiagram(signal), false);
    EXPECT_GT(eye.Panels[0].Series.size(), 10u);
    const auto spectrum = WelchPsd(signal.Samples, signal.SampleRateHz);
    EXPECT_EQ(SpectrumFigure(spectrum, std::vector<double>(spectrum.FrequencyHz.size(), -40)).Panels[0].Series.size(),
              1u);
    const auto stages = BuildPipelineStages(signal);
    const auto pipeline = PipelineFigure(stages, 4, 8, true);
    ASSERT_EQ(pipeline.Panels.size(), 5u);
    EXPECT_EQ(pipeline.Panels[4].XLabel, "Time (symbol periods)");
    EXPECT_TRUE(pipeline.Panels[0].XLabel.empty());
    ASSERT_TRUE(pipeline.Panels[3].XLimits);
    EXPECT_DOUBLE_EQ((*pipeline.Panels[3].XLimits)[0], 4);
    EXPECT_DOUBLE_EQ((*pipeline.Panels[3].XLimits)[1], 12);
    // Every figure renders to both formats.
    for (const auto *figure : {&waveform, &constellation, &eye, &pipeline}) {
        EXPECT_NO_THROW(RenderSvg(*figure, {800, 600}));
        const auto image = Decode(RenderPng(*figure, {800, 600, false}));
        EXPECT_GT(CountNear(image, {51, 153, 255}, 50), 8u);
    }
    // A request beyond the signal is clamped, not an error.
    EXPECT_NO_THROW(PipelineFigure(stages, 1000, 1000, false));
}

TEST(PlotExport, LogarithmicAxisDrawsDecadeTicksAndSkipsNonPositiveValues) {
    Panel panel;
    panel.YLog = true;
    panel.XLabel = "Eb/N0 (dB)";
    panel.YLabel = "BER";
    Series series;
    series.X = {0, 1, 2, 3};
    series.Y = {0.1, 0.01, 0.0, 1e-4}; // The zero cannot be shown on a log axis
    panel.Series.push_back(series);
    const Figure figure{"BER", {panel}};
    const auto svg = RenderSvg(figure);
    EXPECT_NE(svg.find("1e-1"), std::string::npos);
    EXPECT_NE(svg.find("1e-4"), std::string::npos);
    EXPECT_EQ(svg.find("nan"), std::string::npos);
    panel.YLimits = std::array<double, 2>{1e-5, 1};
    EXPECT_NO_THROW(RenderPng(Figure{"BER", {panel}}));
}

} // namespace Core
