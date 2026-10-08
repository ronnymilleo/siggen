/**
 * @file    plot_export.cpp
 * @brief   Renders plots described as data into SVG documents or PNG images.
 * @details One layout routine draws through the Canvas interface, implemented by an SVG writer and by a
 *          supersampled raster that is then encoded as PNG with stb_image_write.
 */

#include "plot_export.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_easy_font.h"
#include "stb_image_write.h"
#include "stb_truetype.h"

namespace Core {

namespace {

// region Fonts

/**
 * @struct  SystemFont
 * @brief   The UI font the application itself prefers, so exported raster text matches the window.
 * @details Loaded once. When none of the candidates is installed, Ok is false and text falls back to a small
 *          built-in bitmap font.
 */
struct SystemFont {
    std::vector<unsigned char> Data;
    stbtt_fontinfo Info{};
    bool Ok = false;

    SystemFont() {
        constexpr const char *Candidates[] = {
#ifdef __APPLE__
            "/System/Library/Fonts/Helvetica.ttc",
#else
            "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            // Arch Linux keeps the same fonts in other folders
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
#endif
            "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
            "C:/Windows/Fonts/segoeui.ttf",
            "C:/Windows/Fonts/arial.ttf",
        };
        for (const char *path : Candidates) {
            std::error_code error;
            if (!std::filesystem::exists(path, error)) {
                continue;
            }
            std::ifstream file(path, std::ios::binary);
            Data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (!Data.empty() && stbtt_InitFont(&Info, Data.data(), stbtt_GetFontOffsetForIndex(Data.data(), 0))) {
                Ok = true;
                return;
            }
        }
    }

    static const SystemFont &Get() {
        static const SystemFont font;
        return font;
    }
};

// Decodes UTF-8; invalid bytes become '?'
std::vector<int> Codepoints(const std::string &text) {
    std::vector<int> codepoints;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const int length = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 0;
        if (!length || i + static_cast<std::size_t>(length) > text.size()) {
            codepoints.push_back('?');
            ++i;
            continue;
        }
        int codepoint = length == 1 ? lead : lead & (0xFF >> (length + 1));
        for (int j = 1; j < length; ++j) {
            codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i + static_cast<std::size_t>(j)]) & 0x3F);
        }
        codepoints.push_back(codepoint);
        i += static_cast<std::size_t>(length);
    }
    return codepoints;
}

// Advance width with kerning, in pixels
double TtfWidth(const stbtt_fontinfo &info, const std::string &text, const double px) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(px));
    double width = 0;
    const auto codepoints = Codepoints(text);
    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        int advance = 0, left_bearing = 0;
        stbtt_GetCodepointHMetrics(&info, codepoints[i], &advance, &left_bearing);
        width += advance * scale;
        if (i + 1 < codepoints.size()) {
            width += stbtt_GetCodepointKernAdvance(&info, codepoints[i], codepoints[i + 1]) * scale;
        }
    }
    return width;
}

// endregion

// region Canvas interface

struct Point {
    double X, Y;
};

enum class Anchor {
    Start,
    Middle,
    End
};

/**
 * @class   Canvas
 * @brief   Everything the layout needs from a backend. Coordinates are output pixels, y down.
 */
class Canvas {
public:
    virtual ~Canvas() = default;

    virtual void FillRect(double x, double y, double w, double h, Rgb colour, double alpha) = 0;
    virtual void Polyline(const std::vector<Point> &points, Rgb colour, double alpha, double width) = 0;
    virtual void Circle(double x, double y, double radius, Rgb colour, double alpha) = 0;
    // (x, y) is the baseline point; `up` rotates the text a quarter turn counter-clockwise
    virtual void Text(double x, double y, const std::string &text, double px, Rgb colour, Anchor anchor, bool up) = 0;
    virtual double TextWidth(const std::string &text, double px) = 0;
    virtual void Clip(double x, double y, double w, double h) = 0;
    virtual void Unclip() = 0;

    void Line(double x0, double y0, double x1, double y1, Rgb colour, double alpha, double width) {
        Polyline({{x0, y0}, {x1, y1}}, colour, alpha, width);
    }
};

// endregion

// region SVG canvas

class SvgCanvas final : public Canvas {
public:
    SvgCanvas(const ImageStyle &style, const std::string &title) {
        m_Document += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        m_Document +=
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(style.Width) + "\" height=\"" +
            std::to_string(style.Height) + "\" viewBox=\"0 0 " + std::to_string(style.Width) + " " +
            std::to_string(style.Height) +
            "\" font-family=\"Inter, 'Noto Sans', 'DejaVu Sans', 'Liberation Sans', Helvetica, Arial, sans-serif\">\n";
        if (!title.empty()) {
            m_Document += "<title>" + Escape(title) + "</title>\n";
        }
    }

    std::string Finish() { return m_Document + "</svg>\n"; }

    void FillRect(double x, double y, double w, double h, Rgb colour, double alpha) override {
        m_Document += "<rect x=\"" + Number(x) + "\" y=\"" + Number(y) + "\" width=\"" + Number(w) + "\" height=\"" +
                      Number(h) + "\" fill=\"" + Hex(colour) + "\"" + Opacity("fill-opacity", alpha) + "/>\n";
    }

    void Polyline(const std::vector<Point> &points, Rgb colour, double alpha, double width) override {
        m_Document += "<polyline fill=\"none\" stroke=\"" + Hex(colour) + "\"" + Opacity("stroke-opacity", alpha) +
                      " stroke-width=\"" + Number(width) +
                      "\" stroke-linejoin=\"round\" stroke-linecap=\"round\" points=\"";
        for (std::size_t i = 0; i < points.size(); ++i) {
            m_Document += (i ? " " : "") + Number(points[i].X) + "," + Number(points[i].Y);
        }
        m_Document += "\"/>\n";
    }

    void Circle(double x, double y, double radius, Rgb colour, double alpha) override {
        m_Document += "<circle cx=\"" + Number(x) + "\" cy=\"" + Number(y) + "\" r=\"" + Number(radius) + "\" fill=\"" +
                      Hex(colour) + "\"" + Opacity("fill-opacity", alpha) + "/>\n";
    }

    void Text(double x, double y, const std::string &text, double px, Rgb colour, Anchor anchor, bool up) override {
        const char *text_anchor = anchor == Anchor::Start ? "start" : anchor == Anchor::Middle ? "middle" : "end";
        m_Document += "<text x=\"" + Number(x) + "\" y=\"" + Number(y) + "\" font-size=\"" + Number(px) + "\" fill=\"" +
                      Hex(colour) + "\" text-anchor=\"" + text_anchor + "\"";
        if (up) {
            m_Document += " transform=\"rotate(-90 " + Number(x) + " " + Number(y) + ")\"";
        }
        m_Document += ">" + Escape(text) + "</text>\n";
    }

    // Estimate for a proportional sans-serif font; the viewer renders the real text
    double TextWidth(const std::string &text, double px) override {
        return 0.56 * px * static_cast<double>(text.size());
    }

    void Clip(double x, double y, double w, double h) override {
        const auto id = "clip" + std::to_string(m_NextClip++);
        m_Document += "<clipPath id=\"" + id + "\"><rect x=\"" + Number(x) + "\" y=\"" + Number(y) + "\" width=\"" +
                      Number(w) + "\" height=\"" + Number(h) + "\"/></clipPath>\n<g clip-path=\"url(#" + id + ")\">\n";
    }

    void Unclip() override { m_Document += "</g>\n"; }

private:
    // Two decimals at most, trailing zeros removed, never "-0"
    static std::string Number(double value) {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.2f", value);
        std::string text = buffer;
        while (text.back() == '0') {
            text.pop_back();
        }
        if (text.back() == '.') {
            text.pop_back();
        }
        return text == "-0" ? "0" : text;
    }

    static std::string Opacity(const char *name, double alpha) {
        return alpha >= 0.999 ? "" : std::string(" ") + name + "=\"" + Number(alpha) + "\"";
    }

    static std::string Hex(Rgb colour) {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", colour.R, colour.G, colour.B);
        return buffer;
    }

    static std::string Escape(const std::string &text) {
        std::string escaped;
        for (char character : text) {
            switch (character) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped += character;
            }
        }
        return escaped;
    }

    std::string m_Document;
    int m_NextClip = 0;
};

// endregion

// region Raster canvas

/**
 * @class   RasterCanvas
 * @brief   RGB canvas that draws at twice the output size and averages down, which antialiases lines, discs and
 *          text alike.
 */
class RasterCanvas final : public Canvas {
public:
    static constexpr int Supersample = 2;

    explicit RasterCanvas(const ImageStyle &style)
        : m_Width(style.Width * Supersample), m_Height(style.Height * Supersample), m_OutputWidth(style.Width),
          m_OutputHeight(style.Height),
          m_Pixels(static_cast<std::size_t>(m_Width) * static_cast<std::size_t>(m_Height) * 3, 255) {
        m_ClipX0 = m_ClipY0 = 0;
        m_ClipX1 = m_Width;
        m_ClipY1 = m_Height;
    }

    // Box-filters the supersampled pixels down to the output size
    std::vector<std::uint8_t> Finish() const {
        std::vector<std::uint8_t> output(static_cast<std::size_t>(m_OutputWidth) *
                                         static_cast<std::size_t>(m_OutputHeight) * 3);
        for (int y = 0; y < m_OutputHeight; ++y) {
            for (int x = 0; x < m_OutputWidth; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    int sum = 0;
                    for (int dy = 0; dy < Supersample; ++dy) {
                        for (int dx = 0; dx < Supersample; ++dx) {
                            sum += m_Pixels[Index(x * Supersample + dx, y * Supersample + dy) +
                                            static_cast<std::size_t>(channel)];
                        }
                    }
                    output[(static_cast<std::size_t>(y) * static_cast<std::size_t>(m_OutputWidth) +
                            static_cast<std::size_t>(x)) *
                               3 +
                           static_cast<std::size_t>(channel)] =
                        static_cast<std::uint8_t>((sum + Supersample * Supersample / 2) / (Supersample * Supersample));
                }
            }
        }
        return output;
    }

    void FillRect(double x, double y, double w, double h, Rgb colour, double alpha) override {
        FillSupersampledRect(x * Supersample, y * Supersample, (x + w) * Supersample, (y + h) * Supersample, colour,
                             alpha);
    }

    void Polyline(const std::vector<Point> &points, Rgb colour, double alpha, double width) override {
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            Segment(points[i].X * Supersample, points[i].Y * Supersample, points[i + 1].X * Supersample,
                    points[i + 1].Y * Supersample, colour, alpha, width * Supersample / 2);
        }
    }

    void Circle(double x, double y, double radius, Rgb colour, double alpha) override {
        Segment(x * Supersample, y * Supersample, x * Supersample, y * Supersample, colour, alpha,
                radius * Supersample);
    }

    void Text(double x, double y, const std::string &text, double px, Rgb colour, Anchor anchor, bool up) override {
        const auto &font = SystemFont::Get();
        if (font.Ok) {
            TtfText(font.Info, x, y, text, px, colour, anchor, up);
        } else {
            BitmapText(x, y, text, px, colour, anchor, up);
        }
    }

    double TextWidth(const std::string &text, double px) override {
        const auto &font = SystemFont::Get();
        if (font.Ok) {
            return TtfWidth(font.Info, text, px);
        }
        std::vector<char> terminated(text.begin(), text.end());
        terminated.push_back('\0');
        return stb_easy_font_width(terminated.data()) * px / 10.0;
    }

    void Clip(double x, double y, double w, double h) override {
        m_ClipX0 = std::max(0, static_cast<int>(std::floor(x * Supersample)));
        m_ClipY0 = std::max(0, static_cast<int>(std::floor(y * Supersample)));
        m_ClipX1 = std::min(m_Width, static_cast<int>(std::ceil((x + w) * Supersample)));
        m_ClipY1 = std::min(m_Height, static_cast<int>(std::ceil((y + h) * Supersample)));
    }

    void Unclip() override {
        m_ClipX0 = m_ClipY0 = 0;
        m_ClipX1 = m_Width;
        m_ClipY1 = m_Height;
    }

private:
    void TtfText(const stbtt_fontinfo &info, double x, double y, const std::string &s, double px, Rgb c, Anchor a,
                 bool up) {
        const float sc = stbtt_ScaleForMappingEmToPixels(&info, static_cast<float>(px * Supersample));
        const double width = TtfWidth(info, s, px * Supersample);
        double pen = a == Anchor::Start ? 0 : a == Anchor::Middle ? -width / 2 : -width;
        const auto cps = Codepoints(s);
        for (std::size_t i = 0; i < cps.size(); ++i) {
            int w = 0, h = 0, xoff = 0, yoff = 0;
            const auto frac = static_cast<float>(pen - std::floor(pen));
            unsigned char *bmp = stbtt_GetCodepointBitmapSubpixel(&info, sc, sc, frac, 0, cps[i], &w, &h, &xoff, &yoff);
            for (int j = 0; j < h; ++j) {
                for (int k = 0; k < w; ++k) {
                    const double lx = std::floor(pen) + xoff + k, ly = yoff + j;
                    const int pixel_x = static_cast<int>(std::lround(up ? x * Supersample + ly : x * Supersample + lx));
                    const int pixel_y = static_cast<int>(std::lround(up ? y * Supersample - lx : y * Supersample + ly));
                    if (pixel_x >= m_ClipX0 && pixel_x < m_ClipX1 && pixel_y >= m_ClipY0 && pixel_y < m_ClipY1) {
                        Blend(pixel_x, pixel_y, c, bmp[j * w + k] / 255.0);
                    }
                }
            }
            if (bmp) {
                stbtt_FreeBitmap(bmp, nullptr);
            }
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, cps[i], &adv, &lsb);
            pen += adv * sc;
            if (i + 1 < cps.size()) {
                pen += stbtt_GetCodepointKernAdvance(&info, cps[i], cps[i + 1]) * sc;
            }
        }
    }

    // Fallback when no system font is installed: stb_easy_font quads, filled as rectangles
    void BitmapText(double x, double y, const std::string &text, double px, Rgb colour, Anchor anchor, bool up) {
        const double scale = px / 10.0 * Supersample;
        std::vector<char> terminated(text.begin(), text.end());
        terminated.push_back('\0');
        std::vector<char> vertex_buffer(2048 * (text.size() + 1) + 256);
        const int quad_count = stb_easy_font_print(0, 0, terminated.data(), nullptr, vertex_buffer.data(),
                                                   static_cast<int>(vertex_buffer.size()));
        const double width = stb_easy_font_width(terminated.data()) * scale;
        const double start_x = anchor == Anchor::Start ? 0 : anchor == Anchor::Middle ? -width / 2 : -width;
        constexpr double CapHeight = 7.0; // Glyph rows above the baseline
        // Vertex layout written by stb_easy_font_print
        struct Vertex {
            float X, Y, Z;
            unsigned char Color[4];
        };
        const auto *vertices = reinterpret_cast<const Vertex *>(vertex_buffer.data());
        for (int quad = 0; quad < quad_count; ++quad) {
            double local_x[2] = {1e30, -1e30}, local_y[2] = {1e30, -1e30};
            for (int i = 0; i < 4; ++i) {
                const double ux = start_x + vertices[quad * 4 + i].X * scale;
                const double uy = (vertices[quad * 4 + i].Y - CapHeight) * scale;
                local_x[0] = std::min(local_x[0], ux);
                local_x[1] = std::max(local_x[1], ux);
                local_y[0] = std::min(local_y[0], uy);
                local_y[1] = std::max(local_y[1], uy);
            }
            if (up) {
                FillSupersampledRect(x * Supersample + local_y[0], y * Supersample - local_x[1],
                                     x * Supersample + local_y[1], y * Supersample - local_x[0], colour, 1);
            } else {
                FillSupersampledRect(x * Supersample + local_x[0], y * Supersample + local_y[0],
                                     x * Supersample + local_x[1], y * Supersample + local_y[1], colour, 1);
            }
        }
    }

    std::size_t Index(int x, int y) const {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_Width) + static_cast<std::size_t>(x)) * 3;
    }

    void Blend(int x, int y, Rgb colour, double alpha) {
        if (alpha <= 0) {
            return;
        }
        auto *pixel = &m_Pixels[Index(x, y)];
        pixel[0] = static_cast<std::uint8_t>(std::lround(pixel[0] + (colour.R - pixel[0]) * alpha));
        pixel[1] = static_cast<std::uint8_t>(std::lround(pixel[1] + (colour.G - pixel[1]) * alpha));
        pixel[2] = static_cast<std::uint8_t>(std::lround(pixel[2] + (colour.B - pixel[2]) * alpha));
    }

    // Area-coverage fill of an axis-aligned rectangle (supersampled coordinates)
    void FillSupersampledRect(double x0, double y0, double x1, double y1, Rgb colour, double alpha) {
        if (x1 < x0) {
            std::swap(x0, x1);
        }
        if (y1 < y0) {
            std::swap(y0, y1);
        }
        const int first_x = std::max(m_ClipX0, static_cast<int>(std::floor(x0)));
        const int end_x = std::min(m_ClipX1, static_cast<int>(std::ceil(x1)));
        const int first_y = std::max(m_ClipY0, static_cast<int>(std::floor(y0)));
        const int end_y = std::min(m_ClipY1, static_cast<int>(std::ceil(y1)));
        for (int y = first_y; y < end_y; ++y) {
            const double coverage_y = std::min(y + 1.0, y1) - std::max<double>(y, y0);
            for (int x = first_x; x < end_x; ++x) {
                const double coverage_x = std::min(x + 1.0, x1) - std::max<double>(x, x0);
                Blend(x, y, colour, alpha * std::clamp(coverage_x, 0.0, 1.0) * std::clamp(coverage_y, 0.0, 1.0));
            }
        }
    }

    // Capsule: every pixel within `radius` of the segment, antialiased at the edge
    void Segment(double x0, double y0, double x1, double y1, Rgb colour, double alpha, double radius) {
        const double margin = radius + 1;
        const int first_x = std::max(m_ClipX0, static_cast<int>(std::floor(std::min(x0, x1) - margin)));
        const int end_x = std::min(m_ClipX1, static_cast<int>(std::ceil(std::max(x0, x1) + margin)));
        const int first_y = std::max(m_ClipY0, static_cast<int>(std::floor(std::min(y0, y1) - margin)));
        const int end_y = std::min(m_ClipY1, static_cast<int>(std::ceil(std::max(y0, y1) + margin)));
        const double dx = x1 - x0, dy = y1 - y0, length_squared = dx * dx + dy * dy;
        for (int y = first_y; y < end_y; ++y) {
            for (int x = first_x; x < end_x; ++x) {
                const double qx = x + 0.5 - x0, qy = y + 0.5 - y0;
                const double t = length_squared > 0 ? std::clamp((qx * dx + qy * dy) / length_squared, 0.0, 1.0) : 0.0;
                const double distance = std::hypot(qx - t * dx, qy - t * dy);
                Blend(x, y, colour, alpha * std::clamp(radius + 0.5 - distance, 0.0, 1.0));
            }
        }
    }

    int m_Width, m_Height, m_OutputWidth, m_OutputHeight;
    std::vector<std::uint8_t> m_Pixels;
    int m_ClipX0, m_ClipY0, m_ClipX1, m_ClipY1;
};

// endregion

// region Layout

/**
 * @struct  Theme
 * @brief   Figure colours. Dark follows the application's own palette (slate surfaces, muted axis text, faint
 *          white grid).
 */
struct Theme {
    Rgb Background, PlotBackground, Foreground, Muted, Border, Grid;
    double GridAlpha;
};

/**
 * @struct  Axes
 * @brief   A plot rectangle in pixels and the data limits it maps.
 */
struct Axes {
    double X0, Y0, W, H;       // Plot rectangle in pixels
    double Xlo, Xhi, Ylo, Yhi; // Data limits

    double Px(double x) const { return X0 + (x - Xlo) / (Xhi - Xlo) * W; }
    double Py(double y) const { return Y0 + H - (y - Ylo) / (Yhi - Ylo) * H; }
};

Theme ThemeFor(const bool dark) {
    if (dark) {
        return {{27, 31, 39}, {19, 22, 28}, {224, 228, 235}, {139, 148, 163}, {52, 59, 72}, {255, 255, 255}, 0.08};
    }
    return {{255, 255, 255}, {247, 248, 250}, {28, 32, 40}, {95, 104, 120}, {205, 210, 220}, {0, 0, 0}, 0.08};
}

double Pow10i(const int exponent) {
    return std::pow(10.0, exponent);
}

/**
 * @brief   Finds the range of the finite values of a panel along one axis.
 * @return  {lo, hi}; stems always include y = 0, and an empty panel gives {0, 1}.
 */
std::array<double, 2> DataRange(const Panel &panel, const bool axis_x) {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const auto &series : panel.Series) {
        const auto &values = axis_x ? series.X : series.Y;
        for (std::size_t i = 0; i < values.size() && i < series.X.size() && i < series.Y.size(); ++i) {
            if (std::isfinite(values[i])) {
                lo = std::min(lo, values[i]);
                hi = std::max(hi, values[i]);
            }
        }
        if (!axis_x && series.Kind == SeriesKind::Stems && !series.Y.empty()) {
            lo = std::min(lo, 0.0);
            hi = std::max(hi, 0.0);
        }
    }
    if (!(lo <= hi)) {
        lo = 0;
        hi = 1;
    }
    return {lo, hi};
}

// Adds a 6 % margin, or opens a degenerate range around its centre
std::array<double, 2> Padded(const std::array<double, 2> &range) {
    const double lo = range[0], hi = range[1];
    if (hi - lo < 1e-300 || !(hi - lo > 1e-12 * std::max(std::abs(lo), std::abs(hi)))) {
        const double centre = (lo + hi) / 2;
        const double half_width = std::max(std::abs(centre) * 0.1, 1e-3 * (centre == 0 ? 1000.0 : 1.0));
        return {centre - half_width, centre + half_width};
    }
    const double pad = (hi - lo) * 0.06;
    return {lo - pad, hi + pad};
}

// Widens one axis so both have the same pixels per unit, keeping the centre
void ApplyEqualAspect(Axes &axes) {
    const double scale = std::min(axes.W / (axes.Xhi - axes.Xlo), axes.H / (axes.Yhi - axes.Ylo));
    const double centre_x = (axes.Xlo + axes.Xhi) / 2, centre_y = (axes.Ylo + axes.Yhi) / 2;
    axes.Xlo = centre_x - axes.W / scale / 2;
    axes.Xhi = centre_x + axes.W / scale / 2;
    axes.Ylo = centre_y - axes.H / scale / 2;
    axes.Yhi = centre_y + axes.H / scale / 2;
}

/**
 * @brief   Draws one series inside the axes.
 * @param[in] scale Figure scale relative to 1600x900, applied to line widths and markers.
 * @note    Non-finite points break lines and stairs into separate runs and are skipped by markers.
 */
void DrawSeries(Canvas &canvas, const Axes &axes, const Series &series, const double scale) {
    const auto count = std::min(series.X.size(), series.Y.size());
    const double width = series.Width * scale;
    auto finite = [&](std::size_t i) { return std::isfinite(series.X[i]) && std::isfinite(series.Y[i]); };
    switch (series.Kind) {
    case SeriesKind::Line:
    case SeriesKind::Stairs: {
        std::vector<Point> run;
        auto flush = [&] {
            if (run.size() >= 2) {
                canvas.Polyline(run, series.Color, series.Alpha, width);
            }
            run.clear();
        };
        for (std::size_t i = 0; i < count; ++i) {
            if (!finite(i)) {
                flush();
                continue;
            }
            const Point point{axes.Px(series.X[i]), axes.Py(series.Y[i])};
            if (series.Kind == SeriesKind::Stairs && !run.empty()) {
                run.push_back({point.X, run.back().Y});
            }
            run.push_back(point);
        }
        flush();
        break;
    }
    case SeriesKind::Scatter:
        for (std::size_t i = 0; i < count; ++i) {
            if (finite(i)) {
                canvas.Circle(axes.Px(series.X[i]), axes.Py(series.Y[i]), std::max(1.2, width * 0.75), series.Color,
                              series.Alpha);
            }
        }
        break;
    case SeriesKind::Stems:
        for (std::size_t i = 0; i < count; ++i) {
            if (finite(i)) {
                canvas.Line(axes.Px(series.X[i]), axes.Py(0), axes.Px(series.X[i]), axes.Py(series.Y[i]), series.Color,
                            series.Alpha, width * 0.75);
                canvas.Circle(axes.Px(series.X[i]), axes.Py(series.Y[i]), std::max(1.5, width * 1.1), series.Color,
                              series.Alpha);
            }
        }
        break;
    }
}

void DrawGrid(Canvas &canvas, const Theme &theme, const Panel &panel, const Axes &axes,
              const std::vector<double> &x_ticks, const std::vector<double> &y_ticks, const double scale) {
    for (double tick : x_ticks) {
        canvas.Line(axes.Px(tick), axes.Y0, axes.Px(tick), axes.Y0 + axes.H, theme.Grid, theme.GridAlpha,
                    std::max(1.0, scale));
    }
    for (double tick : y_ticks) {
        canvas.Line(axes.X0, axes.Py(tick), axes.X0 + axes.W, axes.Py(tick), theme.Grid, theme.GridAlpha,
                    std::max(1.0, scale));
    }
    if (panel.ZeroLine && axes.Ylo < 0 && axes.Yhi > 0) {
        canvas.Line(axes.X0, axes.Py(0), axes.X0 + axes.W, axes.Py(0), theme.Muted, 0.6, std::max(1.2, 1.5 * scale));
    }
}

void DrawFrameAndTicks(Canvas &canvas, const Theme &theme, const Panel &panel, const Axes &axes,
                       const std::vector<double> &x_ticks, const std::vector<double> &y_ticks, const double scale,
                       const double tick_font) {
    const double frame_width = std::max(1.0, 1.4 * scale);
    canvas.Polyline({{axes.X0, axes.Y0},
                     {axes.X0 + axes.W, axes.Y0},
                     {axes.X0 + axes.W, axes.Y0 + axes.H},
                     {axes.X0, axes.Y0 + axes.H},
                     {axes.X0, axes.Y0}},
                    theme.Border, 1, frame_width);
    const double x_step = x_ticks.size() >= 2 ? x_ticks[1] - x_ticks[0] : 1;
    const double y_step = y_ticks.size() >= 2 ? y_ticks[1] - y_ticks[0] : 1;
    for (double tick : x_ticks) {
        canvas.Line(axes.Px(tick), axes.Y0 + axes.H, axes.Px(tick), axes.Y0 + axes.H + 5 * scale, theme.Muted, 0.7,
                    frame_width);
        canvas.Text(axes.Px(tick), axes.Y0 + axes.H + 5 * scale + tick_font * 1.05, FormatTick(tick, x_step), tick_font,
                    theme.Muted, Anchor::Middle, false);
    }
    for (double tick : y_ticks) {
        canvas.Line(axes.X0 - 5 * scale, axes.Py(tick), axes.X0, axes.Py(tick), theme.Muted, 0.7, frame_width);
        const auto label =
            panel.YLog ? "1e" + std::to_string(static_cast<int>(std::lround(tick))) : FormatTick(tick, y_step);
        canvas.Text(axes.X0 - 9 * scale, axes.Py(tick) + tick_font * 0.35, label, tick_font, theme.Muted, Anchor::End,
                    false);
    }
}

void DrawLabels(Canvas &canvas, const Theme &theme, const Panel &panel, const Axes &axes, const double scale,
                const double tick_font) {
    const double label_font = 15.5 * scale, title_font = 16 * scale;
    if (!panel.XLabel.empty()) {
        canvas.Text(axes.X0 + axes.W / 2, axes.Y0 + axes.H + 5 * scale + tick_font * 1.05 + 8 * scale + label_font,
                    panel.XLabel, label_font, theme.Muted, Anchor::Middle, false);
    }
    if (!panel.YLabel.empty()) {
        canvas.Text(axes.X0 - 66 * scale, axes.Y0 + axes.H / 2, panel.YLabel, label_font, theme.Muted, Anchor::Middle,
                    true);
    }
    if (!panel.Title.empty()) {
        canvas.Text(axes.X0, axes.Y0 - 9 * scale, panel.Title, title_font, theme.Foreground, Anchor::Start, false);
    }
}

/**
 * @brief   Draws the legend of the labelled series.
 * @details In the header row, right-aligned, when the panel has a title (stacked panels); otherwise in a box
 *          inside the plot, top right.
 */
void DrawLegend(Canvas &canvas, const Theme &theme, const Panel &panel, const Axes &axes, const double scale,
                const double font) {
    double widest_label = 0, header_width = 0;
    std::size_t rows = 0;
    for (const auto &series : panel.Series) {
        if (!series.Label.empty()) {
            widest_label = std::max(widest_label, canvas.TextWidth(series.Label, font));
            header_width += 46 * scale + canvas.TextWidth(series.Label, font);
            ++rows;
        }
    }
    if (rows && !panel.Title.empty()) {
        double x = axes.X0 + axes.W - header_width;
        for (const auto &series : panel.Series) {
            if (!series.Label.empty()) {
                canvas.Line(x, axes.Y0 - 14 * scale, x + 20 * scale, axes.Y0 - 14 * scale, series.Color, 1,
                            std::max(1.5, series.Width * scale));
                canvas.Text(x + 26 * scale, axes.Y0 - 14 * scale + font * 0.35, series.Label, font, theme.Foreground,
                            Anchor::Start, false);
                x += 46 * scale + canvas.TextWidth(series.Label, font);
            }
        }
    } else if (rows) {
        const double row = 21 * scale, box_width = 44 * scale + widest_label;
        const double box_height = row * static_cast<double>(rows) + 8 * scale;
        const double box_x = axes.X0 + axes.W - box_width - 10 * scale, box_y = axes.Y0 + 10 * scale;
        canvas.FillRect(box_x, box_y, box_width, box_height, theme.Background, 0.9);
        canvas.Polyline({{box_x, box_y},
                         {box_x + box_width, box_y},
                         {box_x + box_width, box_y + box_height},
                         {box_x, box_y + box_height},
                         {box_x, box_y}},
                        theme.Border, 1, std::max(1.0, scale));
        double y = box_y + 4 * scale + row / 2;
        for (const auto &series : panel.Series) {
            if (!series.Label.empty()) {
                canvas.Line(box_x + 8 * scale, y, box_x + 28 * scale, y, series.Color, 1,
                            std::max(1.5, series.Width * scale));
                canvas.Text(box_x + 36 * scale, y + font * 0.35, series.Label, font, theme.Foreground, Anchor::Start,
                            false);
                y += row;
            }
        }
    }
}

// The axes of a logarithmic panel hold log10(y): a tick at each decade, thinned when the range spans many
std::vector<double> DecadeTicks(const Axes &axes, const double scale) {
    const int first = static_cast<int>(std::ceil(axes.Ylo - 1e-9));
    const int last = static_cast<int>(std::floor(axes.Yhi + 1e-9));
    const int stride = std::max(1, (last - first + 1) / std::clamp(static_cast<int>(axes.H / (40 * scale)), 2, 10));
    std::vector<double> ticks;
    for (int decade = first; decade <= last; decade += stride) {
        ticks.push_back(decade);
    }
    return ticks;
}

// A logarithmic panel is drawn as log10(y) on a linear axis; values that are not positive vanish
Panel ToLogScale(const Panel &panel) {
    Panel shown = panel;
    for (auto &series : shown.Series) {
        for (auto &value : series.Y) {
            value = value > 0 ? std::log10(value) : std::numeric_limits<double>::quiet_NaN();
        }
    }
    if (panel.YLimits) {
        shown.YLimits = std::array<double, 2>{std::log10(std::max((*panel.YLimits)[0], 1e-300)),
                                              std::log10(std::max((*panel.YLimits)[1], 1e-300))};
    }
    return shown;
}

/**
 * @brief   Draws a panel: background, grid, series (clipped to the plot), frame, ticks, labels and legend.
 * @param[in] axes  Plot rectangle and data limits; widened here when the panel asks for an equal aspect.
 * @param[in] scale Figure scale relative to 1600x900.
 */
void DrawPanel(Canvas &canvas, const Theme &theme, const Panel &panel, Axes axes, const double scale) {
    const double tick_font = 13 * scale;
    if (panel.EqualAspect) {
        ApplyEqualAspect(axes);
    }
    canvas.FillRect(axes.X0, axes.Y0, axes.W, axes.H, theme.PlotBackground, 1);
    const auto x_ticks = NiceTicks(axes.Xlo, axes.Xhi, std::clamp(static_cast<int>(axes.W / (95 * scale)), 3, 12));
    const auto y_ticks =
        panel.YLog ? DecadeTicks(axes, scale)
                   : NiceTicks(axes.Ylo, axes.Yhi, std::clamp(static_cast<int>(axes.H / (55 * scale)), 2, 10));
    DrawGrid(canvas, theme, panel, axes, x_ticks, y_ticks, scale);
    canvas.Clip(axes.X0, axes.Y0, axes.W, axes.H);
    for (const auto &series : panel.Series) {
        DrawSeries(canvas, axes, series, scale);
    }
    canvas.Unclip();
    DrawFrameAndTicks(canvas, theme, panel, axes, x_ticks, y_ticks, scale, tick_font);
    DrawLabels(canvas, theme, panel, axes, scale, tick_font);
    DrawLegend(canvas, theme, panel, axes, scale, tick_font);
}

/**
 * @brief   Lays out a figure: background, title, then the panels stacked with equal plot heights.
 * @note    Sizes are designed for 1600x900 and scaled with the smaller ratio, clamped to [0.45, 3].
 */
void DrawFigure(Canvas &canvas, const Figure &figure, const ImageStyle &style) {
    const double width = style.Width, height = style.Height;
    const double scale = std::clamp(std::min(width / 1600.0, height / 900.0), 0.45, 3.0);
    const Theme theme = ThemeFor(style.Dark);
    canvas.FillRect(0, 0, width, height, theme.Background, 1);
    const double top = figure.Title.empty() ? 20 * scale : 62 * scale, bottom = 16 * scale, left = 104 * scale,
                 right = 30 * scale;
    if (!figure.Title.empty()) {
        canvas.Text(width / 2, 38 * scale, figure.Title, 22 * scale, theme.Foreground, Anchor::Middle, false);
    }
    const double tick_row = 5 * scale + 13 * scale * 1.05 + 14 * scale;
    double overhead = 0;
    for (const auto &panel : figure.Panels) {
        overhead +=
            (panel.Title.empty() ? 10 * scale : 34 * scale) + tick_row + (panel.XLabel.empty() ? 0 : 28 * scale);
    }
    const double plot_height =
        std::max(20.0, (height - top - bottom - overhead) / static_cast<double>(figure.Panels.size()));
    double y = top;
    for (const auto &panel : figure.Panels) {
        y += panel.Title.empty() ? 10 * scale : 34 * scale;
        const Panel shown = panel.YLog ? ToLogScale(panel) : panel;
        const auto x_range = shown.XLimits ? *shown.XLimits : Padded(DataRange(shown, true));
        const auto y_data = DataRange(shown, false);
        auto y_range = Padded(y_data);
        if (shown.YLog && !panel.YLimits) {
            // Whole decades, at least one
            y_range = {std::floor(y_data[0]), std::max(std::ceil(y_data[1]), std::floor(y_data[0]) + 1)};
        }
        if (shown.YLimits) {
            y_range = *shown.YLimits;
        }
        const Axes axes{left, y, width - left - right, plot_height, x_range[0], x_range[1], y_range[0], y_range[1]};
        DrawPanel(canvas, theme, shown, axes, scale);
        y += plot_height + tick_row + (panel.XLabel.empty() ? 0 : 28 * scale);
    }
}

void ValidateFigure(const Figure &figure, const ImageStyle &style) {
    if (style.Width < MinImageSize || style.Height < MinImageSize || style.Width > MaxImageSize ||
        style.Height > MaxImageSize) {
        throw std::invalid_argument("Image width and height must be between " + std::to_string(MinImageSize) + " and " +
                                    std::to_string(MaxImageSize) + " pixels");
    }
    if (figure.Panels.empty()) {
        throw std::invalid_argument("Figure has no panels");
    }
}

// endregion

} // namespace

/**
 * @brief   Renders a figure as an SVG document.
 * @param[in] figure    Figure with at least one panel.
 * @param[in] style     Size, between MinImageSize and MaxImageSize pixels on each side, and theme.
 * @return  The SVG text.
 * @note    Throws std::invalid_argument for a size out of range or an empty figure.
 */
std::string RenderSvg(const Figure &figure, const ImageStyle &style) {
    ValidateFigure(figure, style);
    SvgCanvas canvas(style, figure.Title);
    DrawFigure(canvas, figure, style);
    return canvas.Finish();
}

/**
 * @brief   Renders a figure as antialiased RGB pixels.
 * @param[in] figure    Figure with at least one panel.
 * @param[in] style     Size and theme, as in RenderSvg().
 * @return  Row-major RGB bytes, width * height * 3.
 * @note    Throws std::invalid_argument for a size out of range or an empty figure.
 */
std::vector<std::uint8_t> RenderRgb(const Figure &figure, const ImageStyle &style) {
    ValidateFigure(figure, style);
    RasterCanvas canvas(style);
    DrawFigure(canvas, figure, style);
    return canvas.Finish();
}

/**
 * @brief   Renders a figure as a PNG image.
 * @param[in] figure    Figure with at least one panel.
 * @param[in] style     Size and theme, as in RenderSvg().
 * @return  The PNG file bytes.
 * @note    Throws std::invalid_argument like RenderRgb() and std::runtime_error when encoding fails.
 */
std::vector<std::uint8_t> RenderPng(const Figure &figure, const ImageStyle &style) {
    const auto rgb = RenderRgb(figure, style);
    std::vector<std::uint8_t> png;
    const auto sink = [](void *context, void *data, int size) {
        auto *output = static_cast<std::vector<std::uint8_t> *>(context);
        output->insert(output->end(), static_cast<const std::uint8_t *>(data),
                       static_cast<const std::uint8_t *>(data) + size);
    };
    if (!stbi_write_png_to_func(sink, &png, style.Width, style.Height, 3, rgb.data(), style.Width * 3)) {
        throw std::runtime_error("PNG encoding failed");
    }
    return png;
}

/**
 * @brief   Renders a figure and writes it to an image file.
 * @param[in] destination   Image file path; must not be empty.
 * @param[in] figure        Figure with at least one panel.
 * @param[in] format        PNG or SVG.
 * @param[in] style         Size and theme, as in RenderSvg().
 * @param[in] overwrite     Replaces an existing file; otherwise refuses it, with exclusive creation guarding
 *                          against a file appearing after a UI check.
 * @note    The figure is rendered before the file is opened, so a bad figure never leaves an empty file behind.
 *          Throws std::invalid_argument for an empty destination or a bad figure, std::runtime_error when the file
 *          exists and @p overwrite is false, and std::ios_base::failure on I/O errors.
 */
void ExportFigure(const std::filesystem::path &destination, const Figure &figure, const ImageFormat format,
                  const ImageStyle &style, const bool overwrite) {
    if (destination.empty()) {
        throw std::invalid_argument("Image destination is empty");
    }
    std::vector<std::uint8_t> bytes;
    if (format == ImageFormat::PNG) {
        bytes = RenderPng(figure, style);
    } else {
        const auto svg = RenderSvg(figure, style);
        bytes.assign(svg.begin(), svg.end());
    }
    if (!overwrite && std::filesystem::exists(destination)) {
        throw std::runtime_error("Image destination already exists; confirm overwrite");
    }
    std::ofstream file;
    file.exceptions(std::ios::badbit | std::ios::failbit);
    file.open(destination, std::ios::binary | (overwrite ? std::ios::trunc : std::ios::noreplace));
    file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
}

/**
 * @brief   Computes "nice" tick positions (1, 2 or 5 times a power of ten) covering a range.
 * @param[in] lo            Lower end of the range.
 * @param[in] hi            Upper end; must be above @p lo.
 * @param[in] target_count  Approximate number of intervals wanted; at least 1.
 * @return  Ticks inside [lo, hi] in ascending order, at most 64; empty for an invalid range or count.
 */
std::vector<double> NiceTicks(const double lo, const double hi, const int target_count) {
    std::vector<double> ticks;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi) || target_count < 1) {
        return ticks;
    }
    const double raw_step = (hi - lo) / target_count;
    const double magnitude = Pow10i(static_cast<int>(std::floor(std::log10(raw_step))));
    const double normalized = raw_step / magnitude;
    const double step = (normalized <= 1.5 ? 1 : normalized <= 3 ? 2 : normalized <= 7 ? 5 : 10) * magnitude;
    const double tolerance = step * 1e-9;
    for (double tick = std::ceil(lo / step - 1e-9) * step; tick <= hi + tolerance && ticks.size() < 64; tick += step) {
        ticks.push_back(std::abs(tick) < step * 1e-9 ? 0.0 : tick);
    }
    return ticks;
}

/**
 * @brief   Formats a tick label for an axis whose ticks are a given step apart.
 * @param[in] value Tick value.
 * @param[in] step  Distance between ticks; sets the number of decimals shown.
 * @return  "0" for values that round to zero; scientific notation ("2e-5") below 1e-3 or from 1e6 in magnitude;
 *          otherwise fixed notation with the fewest decimals that show the step exactly.
 */
std::string FormatTick(const double value, const double step) {
    const double magnitude = std::abs(value);
    if (magnitude < std::abs(step) * 1e-6 || magnitude == 0) {
        return "0";
    }
    char buffer[48];
    if (magnitude >= 1e6 || magnitude < 1e-3) {
        const int value_exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        const int step_exponent = static_cast<int>(std::floor(std::log10(std::abs(step)) + 1e-9));
        std::snprintf(buffer, sizeof buffer, "%.*e", std::clamp(value_exponent - step_exponent, 0, 6), value);
        const std::string text = buffer;
        const auto exponent_position = text.find('e');
        const int exponent = std::atoi(text.c_str() + exponent_position + 1);
        return text.substr(0, exponent_position) + "e" + std::to_string(exponent);
    }
    int decimals = 0;
    while (decimals < 8 && std::abs(std::abs(step) * Pow10i(decimals) - std::round(std::abs(step) * Pow10i(decimals))) >
                               1e-6 * std::abs(step) * Pow10i(decimals)) {
        ++decimals;
    }
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    const std::string text = buffer;
    if (text.find_first_not_of("-0.") == std::string::npos) {
        return "0";
    }
    return text;
}

} // namespace Core
