#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace iq {
// A plot described as data, drawn the same way into an SVG document or a PNG
// raster so a figure can go straight into a slide or a report. It does not
// depend on the GUI: the application builds a Figure from the data it plots.
struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

struct Series {
    enum class Kind { Line, Scatter, Stairs, Stems };
    Kind kind = Kind::Line;
    std::string label;          // Empty: no legend entry.
    std::vector<double> x, y;
    Rgb color{51, 153, 255};
    double alpha = 1.0;
    double width = 2.0;         // Line width in pixels at 1600x900; markers scale with it.
};

struct Panel {
    std::string title, x_label, y_label;
    std::vector<Series> series;
    std::optional<std::array<double, 2>> x_limits, y_limits; // Empty: fit the data.
    bool equal_aspect = false;  // Same pixels per unit on both axes (constellations).
    bool zero_line = false;     // Emphasise y = 0.
};

struct Figure {
    std::string title;
    std::vector<Panel> panels; // Stacked top to bottom; each has its own axes.
};

struct ImageStyle {
    int width = 1600;
    int height = 900;
    bool dark = true;          // Dark is the application's own look; light (white) suits print.
};

enum class ImageFormat { PNG, SVG };

constexpr int MIN_IMAGE_SIZE = 200;
constexpr int MAX_IMAGE_SIZE = 8192;

// Throws std::invalid_argument for a size outside [MIN_IMAGE_SIZE, MAX_IMAGE_SIZE] or an empty figure.
std::string render_svg(const Figure& figure, const ImageStyle& style = {});
// RGB bytes, row-major, width * height * 3, antialiased.
std::vector<std::uint8_t> render_rgb(const Figure& figure, const ImageStyle& style = {});
std::vector<std::uint8_t> render_png(const Figure& figure, const ImageStyle& style = {});

// Writes the figure; refuses to replace an existing file unless `overwrite`
// (exclusive creation guards against a file appearing after a UI check).
void export_figure(const std::filesystem::path& destination, const Figure& figure, ImageFormat format,
                   const ImageStyle& style = {}, bool overwrite = false);

// "Nice" tick positions (1, 2 or 5 times a power of ten) covering [lo, hi], inside the range.
std::vector<double> nice_ticks(double lo, double hi, int target_count = 6);
// Tick label for a value on an axis whose ticks are `step` apart.
std::string format_tick(double value, double step);
}
