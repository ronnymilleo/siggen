/**
 * @file    plot_export.h
 * @brief   Renders plots described as data into SVG documents or PNG images.
 * @details A figure is drawn the same way into either format, so it can go straight into a slide or a report. It
 *          does not depend on the GUI: the application builds a Figure from the data it plots.
 */

#ifndef SIGGEN_PLOT_EXPORT_H
#define SIGGEN_PLOT_EXPORT_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Core {

/**
 * @struct  Rgb
 * @brief   An 8-bit sRGB colour.
 */
struct Rgb {
    std::uint8_t R = 0, G = 0, B = 0;
};

/**
 * @enum    SeriesKind
 * @brief   How a series is drawn: connected line, markers, steps that hold each value, or stems from y = 0.
 */
enum class SeriesKind {
    Line,
    Scatter,
    Stairs,
    Stems
};

/**
 * @struct  Series
 * @brief   One data series of a panel.
 */
struct Series {
    Core::SeriesKind Kind = Core::SeriesKind::Line;
    std::string Label; // Empty: no legend entry
    std::vector<double> X, Y;
    Rgb Color{51, 153, 255};
    double Alpha = 1.0;
    double Width = 2.0; // Line width in pixels at 1600x900; markers scale with it
};

/**
 * @struct  Panel
 * @brief   One set of axes with its series.
 */
struct Panel {
    std::string Title, XLabel, YLabel;
    std::vector<Core::Series> Series;
    std::optional<std::array<double, 2>> XLimits, YLimits; // Empty: fit the data
    bool EqualAspect = false;                              // Same pixels per unit on both axes (constellations)
    bool ZeroLine = false;                                 // Emphasise y = 0
};

/**
 * @struct  Figure
 * @brief   A titled stack of panels.
 */
struct Figure {
    std::string Title;
    std::vector<Panel> Panels; // Stacked top to bottom; each has its own axes
};

/**
 * @struct  ImageStyle
 * @brief   Output size in pixels and colour theme.
 */
struct ImageStyle {
    int Width = 1600;
    int Height = 900;
    bool Dark = true; // Dark is the application's own look; light (white) suits print
};

/**
 * @enum    ImageFormat
 * @brief   Image file formats ExportFigure() writes.
 */
enum class ImageFormat {
    PNG,
    SVG
};

constexpr int MinImageSize = 200;
constexpr int MaxImageSize = 8192;

// Rendering
std::string RenderSvg(const Figure &figure, const ImageStyle &style = {});
std::vector<std::uint8_t> RenderRgb(const Figure &figure, const ImageStyle &style = {});
std::vector<std::uint8_t> RenderPng(const Figure &figure, const ImageStyle &style = {});
void ExportFigure(const std::filesystem::path &destination, const Figure &figure, ImageFormat format,
                  const ImageStyle &style = {}, bool overwrite = false);

// Axis ticks
std::vector<double> NiceTicks(double lo, double hi, int target_count = 6);
std::string FormatTick(double value, double step);

} // namespace Core

#endif // SIGGEN_PLOT_EXPORT_H
