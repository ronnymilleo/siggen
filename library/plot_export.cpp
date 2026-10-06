#include "plot_export.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_easy_font.h"
#include "stb_image_write.h"

namespace iq {
namespace {
struct Pt {
    double x, y;
};
enum class Anchor { Start, Middle, End };

// Everything the layout needs from a backend. Coordinates are output pixels, y down.
class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void fill_rect(double x, double y, double w, double h, Rgb c, double alpha) = 0;
    virtual void polyline(const std::vector<Pt>& points, Rgb c, double alpha, double width) = 0;
    virtual void circle(double x, double y, double r, Rgb c, double alpha) = 0;
    // (x, y) is the baseline point; `up` rotates the text a quarter turn counter-clockwise.
    virtual void text(double x, double y, const std::string& s, double px, Rgb c, Anchor a, bool up) = 0;
    virtual double text_width(const std::string& s, double px) = 0;
    virtual void clip(double x, double y, double w, double h) = 0;
    virtual void unclip() = 0;
    void line(double x0, double y0, double x1, double y1, Rgb c, double alpha, double width) {
        polyline({{x0, y0}, {x1, y1}}, c, alpha, width);
    }
};

struct Theme {
    Rgb background, plot_background, foreground, grid;
};
Theme theme_for(bool dark) {
    if (dark) return {{24, 26, 31}, {32, 35, 41}, {232, 234, 238}, {64, 68, 77}};
    return {{255, 255, 255}, {255, 255, 255}, {32, 36, 44}, {226, 229, 234}};
}

double pow10i(int e) { return std::pow(10.0, e); }

void bounds_of(const Panel& panel, bool axis_x, double& lo, double& hi) {
    lo = INFINITY;
    hi = -INFINITY;
    for (const auto& s : panel.series) {
        const auto& v = axis_x ? s.x : s.y;
        for (std::size_t i = 0; i < v.size() && i < s.x.size() && i < s.y.size(); ++i)
            if (std::isfinite(v[i])) {
                lo = std::min(lo, v[i]);
                hi = std::max(hi, v[i]);
            }
        if (!axis_x && s.kind == Series::Kind::Stems && !s.y.empty()) {
            lo = std::min(lo, 0.0);
            hi = std::max(hi, 0.0);
        }
    }
    if (!(lo <= hi)) {
        lo = 0;
        hi = 1;
    }
}

std::array<double, 2> padded(double lo, double hi) {
    if (hi - lo < 1e-300 || !(hi - lo > 1e-12 * std::max(std::abs(lo), std::abs(hi)))) {
        const double c = (lo + hi) / 2, r = std::max(std::abs(c) * 0.1, 1e-3 * (c == 0 ? 1000.0 : 1.0));
        return {c - r, c + r};
    }
    const double pad = (hi - lo) * 0.06;
    return {lo - pad, hi + pad};
}

struct Axes {
    double x0, y0, w, h;       // Plot rectangle in pixels.
    double xlo, xhi, ylo, yhi; // Data limits.
    double px(double x) const { return x0 + (x - xlo) / (xhi - xlo) * w; }
    double py(double y) const { return y0 + h - (y - ylo) / (yhi - ylo) * h; }
};

void draw_series(Canvas& c, const Axes& a, const Series& s, double k) {
    const auto n = std::min(s.x.size(), s.y.size());
    const double width = s.width * k;
    auto fin = [&](std::size_t i) { return std::isfinite(s.x[i]) && std::isfinite(s.y[i]); };
    switch (s.kind) {
    case Series::Kind::Line:
    case Series::Kind::Stairs: {
        std::vector<Pt> run;
        auto flush = [&] {
            if (run.size() >= 2) c.polyline(run, s.color, s.alpha, width);
            run.clear();
        };
        for (std::size_t i = 0; i < n; ++i) {
            if (!fin(i)) {
                flush();
                continue;
            }
            const Pt p{a.px(s.x[i]), a.py(s.y[i])};
            if (s.kind == Series::Kind::Stairs && !run.empty()) run.push_back({p.x, run.back().y});
            run.push_back(p);
        }
        flush();
        break;
    }
    case Series::Kind::Scatter:
        for (std::size_t i = 0; i < n; ++i)
            if (fin(i)) c.circle(a.px(s.x[i]), a.py(s.y[i]), std::max(1.2, width * 0.75), s.color, s.alpha);
        break;
    case Series::Kind::Stems:
        for (std::size_t i = 0; i < n; ++i)
            if (fin(i)) {
                c.line(a.px(s.x[i]), a.py(0), a.px(s.x[i]), a.py(s.y[i]), s.color, s.alpha, width * 0.75);
                c.circle(a.px(s.x[i]), a.py(s.y[i]), std::max(1.5, width * 1.1), s.color, s.alpha);
            }
        break;
    }
}

void draw_panel(Canvas& c, const Theme& th, const Panel& panel, Axes a, double k, bool equal) {
    const double ft = 13 * k, fl = 15.5 * k, fp = 16 * k;
    if (equal) {
        const double s = std::min(a.w / (a.xhi - a.xlo), a.h / (a.yhi - a.ylo));
        const double cx = (a.xlo + a.xhi) / 2, cy = (a.ylo + a.yhi) / 2;
        a.xlo = cx - a.w / s / 2; a.xhi = cx + a.w / s / 2;
        a.ylo = cy - a.h / s / 2; a.yhi = cy + a.h / s / 2;
    }
    c.fill_rect(a.x0, a.y0, a.w, a.h, th.plot_background, 1);
    const auto xt = nice_ticks(a.xlo, a.xhi, std::clamp(static_cast<int>(a.w / (95 * k)), 3, 12));
    const auto yt = nice_ticks(a.ylo, a.yhi, std::clamp(static_cast<int>(a.h / (55 * k)), 2, 10));
    for (double t : xt) c.line(a.px(t), a.y0, a.px(t), a.y0 + a.h, th.grid, 1, std::max(1.0, k));
    for (double t : yt) c.line(a.x0, a.py(t), a.x0 + a.w, a.py(t), th.grid, 1, std::max(1.0, k));
    if (panel.zero_line && a.ylo < 0 && a.yhi > 0) c.line(a.x0, a.py(0), a.x0 + a.w, a.py(0), th.foreground, 0.45, std::max(1.2, 1.5 * k));
    c.clip(a.x0, a.y0, a.w, a.h);
    for (const auto& s : panel.series) draw_series(c, a, s, k);
    c.unclip();
    // Frame.
    const double fw = std::max(1.0, 1.4 * k);
    c.polyline({{a.x0, a.y0}, {a.x0 + a.w, a.y0}, {a.x0 + a.w, a.y0 + a.h}, {a.x0, a.y0 + a.h}, {a.x0, a.y0}}, th.foreground, 0.7, fw);
    const double xstep = xt.size() >= 2 ? xt[1] - xt[0] : 1, ystep = yt.size() >= 2 ? yt[1] - yt[0] : 1;
    for (double t : xt) {
        c.line(a.px(t), a.y0 + a.h, a.px(t), a.y0 + a.h + 5 * k, th.foreground, 0.7, fw);
        c.text(a.px(t), a.y0 + a.h + 5 * k + ft * 1.05, format_tick(t, xstep), ft, th.foreground, Anchor::Middle, false);
    }
    for (double t : yt) {
        c.line(a.x0 - 5 * k, a.py(t), a.x0, a.py(t), th.foreground, 0.7, fw);
        c.text(a.x0 - 9 * k, a.py(t) + ft * 0.35, format_tick(t, ystep), ft, th.foreground, Anchor::End, false);
    }
    if (!panel.x_label.empty())
        c.text(a.x0 + a.w / 2, a.y0 + a.h + 5 * k + ft * 1.05 + 8 * k + fl, panel.x_label, fl, th.foreground, Anchor::Middle, false);
    if (!panel.y_label.empty())
        c.text(a.x0 - 66 * k, a.y0 + a.h / 2, panel.y_label, fl, th.foreground, Anchor::Middle, true);
    if (!panel.title.empty()) c.text(a.x0, a.y0 - 9 * k, panel.title, fp, th.foreground, Anchor::Start, false);
    // Legend: in the header row when the panel has a title (stacked panels), otherwise inside the plot, top right.
    double text_w = 0, header_w = 0;
    std::size_t rows = 0;
    for (const auto& s : panel.series)
        if (!s.label.empty()) {
            text_w = std::max(text_w, c.text_width(s.label, ft));
            header_w += 46 * k + c.text_width(s.label, ft);
            ++rows;
        }
    if (rows && !panel.title.empty()) {
        double x = a.x0 + a.w - header_w;
        for (const auto& s : panel.series)
            if (!s.label.empty()) {
                c.line(x, a.y0 - 14 * k, x + 20 * k, a.y0 - 14 * k, s.color, 1, std::max(1.5, s.width * k));
                c.text(x + 26 * k, a.y0 - 14 * k + ft * 0.35, s.label, ft, th.foreground, Anchor::Start, false);
                x += 46 * k + c.text_width(s.label, ft);
            }
    } else if (rows) {
        const double row = 21 * k, bw = 44 * k + text_w, bh = row * static_cast<double>(rows) + 8 * k;
        const double bx = a.x0 + a.w - bw - 10 * k, by = a.y0 + 10 * k;
        c.fill_rect(bx, by, bw, bh, th.plot_background, 0.88);
        c.polyline({{bx, by}, {bx + bw, by}, {bx + bw, by + bh}, {bx, by + bh}, {bx, by}}, th.grid, 1, std::max(1.0, k));
        double y = by + 4 * k + row / 2;
        for (const auto& s : panel.series)
            if (!s.label.empty()) {
                c.line(bx + 8 * k, y, bx + 28 * k, y, s.color, 1, std::max(1.5, s.width * k));
                c.text(bx + 36 * k, y + ft * 0.35, s.label, ft, th.foreground, Anchor::Start, false);
                y += row;
            }
    }
}

void draw_figure(Canvas& c, const Figure& fig, const ImageStyle& st) {
    const double W = st.width, H = st.height;
    const double k = std::clamp(std::min(W / 1600.0, H / 900.0), 0.45, 3.0);
    const Theme th = theme_for(st.dark);
    c.fill_rect(0, 0, W, H, th.background, 1);
    const double top = fig.title.empty() ? 20 * k : 62 * k, bottom = 16 * k, left = 104 * k, right = 30 * k;
    if (!fig.title.empty()) c.text(W / 2, 38 * k, fig.title, 22 * k, th.foreground, Anchor::Middle, false);
    const double tick_row = 5 * k + 13 * k * 1.05 + 14 * k;
    double overhead = 0;
    for (const auto& p : fig.panels)
        overhead += (p.title.empty() ? 10 * k : 34 * k) + tick_row + (p.x_label.empty() ? 0 : 28 * k);
    const double plot_h = std::max(20.0, (H - top - bottom - overhead) / static_cast<double>(fig.panels.size()));
    double y = top;
    for (const auto& p : fig.panels) {
        y += p.title.empty() ? 10 * k : 34 * k;
        Axes a{left, y, W - left - right, plot_h, 0, 1, 0, 1};
        double lo, hi;
        bounds_of(p, true, lo, hi);
        auto xr = padded(lo, hi);
        bounds_of(p, false, lo, hi);
        auto yr = padded(lo, hi);
        if (p.x_limits) xr = *p.x_limits;
        if (p.y_limits) yr = *p.y_limits;
        a.xlo = xr[0]; a.xhi = xr[1]; a.ylo = yr[0]; a.yhi = yr[1];
        draw_panel(c, th, p, a, k, p.equal_aspect);
        y += plot_h + tick_row + (p.x_label.empty() ? 0 : 28 * k);
    }
}

void validate(const Figure& fig, const ImageStyle& st) {
    if (st.width < MIN_IMAGE_SIZE || st.height < MIN_IMAGE_SIZE || st.width > MAX_IMAGE_SIZE || st.height > MAX_IMAGE_SIZE)
        throw std::invalid_argument("Image width and height must be between " + std::to_string(MIN_IMAGE_SIZE) + " and " +
                                    std::to_string(MAX_IMAGE_SIZE) + " pixels");
    if (fig.panels.empty()) throw std::invalid_argument("Figure has no panels");
}

// ---------------------------------------------------------------- SVG
class SvgCanvas final : public Canvas {
public:
    SvgCanvas(const ImageStyle& st, const std::string& title) {
        out_ += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out_ += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" + std::to_string(st.width) + "\" height=\"" +
                std::to_string(st.height) + "\" viewBox=\"0 0 " + std::to_string(st.width) + " " + std::to_string(st.height) +
                "\" font-family=\"Helvetica, Arial, sans-serif\">\n";
        if (!title.empty()) out_ += "<title>" + escape(title) + "</title>\n";
    }
    std::string finish() { return out_ + "</svg>\n"; }
    void fill_rect(double x, double y, double w, double h, Rgb c, double alpha) override {
        out_ += "<rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) + "\" fill=\"" + hex(c) +
                "\"" + opacity("fill-opacity", alpha) + "/>\n";
    }
    void polyline(const std::vector<Pt>& pts, Rgb c, double alpha, double width) override {
        out_ += "<polyline fill=\"none\" stroke=\"" + hex(c) + "\"" + opacity("stroke-opacity", alpha) + " stroke-width=\"" + num(width) +
                "\" stroke-linejoin=\"round\" stroke-linecap=\"round\" points=\"";
        for (std::size_t i = 0; i < pts.size(); ++i) out_ += (i ? " " : "") + num(pts[i].x) + "," + num(pts[i].y);
        out_ += "\"/>\n";
    }
    void circle(double x, double y, double r, Rgb c, double alpha) override {
        out_ += "<circle cx=\"" + num(x) + "\" cy=\"" + num(y) + "\" r=\"" + num(r) + "\" fill=\"" + hex(c) + "\"" + opacity("fill-opacity", alpha) + "/>\n";
    }
    void text(double x, double y, const std::string& s, double px, Rgb c, Anchor a, bool up) override {
        const char* anchor = a == Anchor::Start ? "start" : a == Anchor::Middle ? "middle" : "end";
        out_ += "<text x=\"" + num(x) + "\" y=\"" + num(y) + "\" font-size=\"" + num(px) + "\" fill=\"" + hex(c) + "\" text-anchor=\"" + anchor + "\"";
        if (up) out_ += " transform=\"rotate(-90 " + num(x) + " " + num(y) + ")\"";
        out_ += ">" + escape(s) + "</text>\n";
    }
    double text_width(const std::string& s, double px) override { return 0.56 * px * static_cast<double>(s.size()); }
    void clip(double x, double y, double w, double h) override {
        const auto id = "clip" + std::to_string(next_clip_++);
        out_ += "<clipPath id=\"" + id + "\"><rect x=\"" + num(x) + "\" y=\"" + num(y) + "\" width=\"" + num(w) + "\" height=\"" + num(h) +
                "\"/></clipPath>\n<g clip-path=\"url(#" + id + ")\">\n";
    }
    void unclip() override { out_ += "</g>\n"; }

private:
    static std::string num(double v) {
        char b[32];
        std::snprintf(b, sizeof b, "%.2f", v);
        std::string s = b;
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
        return s == "-0" ? "0" : s;
    }
    static std::string opacity(const char* name, double a) { return a >= 0.999 ? "" : std::string(" ") + name + "=\"" + num(a) + "\""; }
    static std::string hex(Rgb c) {
        char b[8];
        std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
        return b;
    }
    static std::string escape(const std::string& s) {
        std::string r;
        for (char ch : s) switch (ch) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r += ch;
            }
        return r;
    }
    std::string out_;
    int next_clip_ = 0;
};

// ---------------------------------------------------------------- Raster
// Draws at 2x and averages down, which antialiases lines, discs and text alike.
class RasterCanvas final : public Canvas {
public:
    static constexpr int SS = 2;
    explicit RasterCanvas(const ImageStyle& st)
        : w_(st.width * SS), h_(st.height * SS), out_w_(st.width), out_h_(st.height),
          px_(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_) * 3, 255) {
        cx0_ = cy0_ = 0;
        cx1_ = w_;
        cy1_ = h_;
    }
    std::vector<std::uint8_t> finish() const {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(out_w_) * static_cast<std::size_t>(out_h_) * 3);
        for (int y = 0; y < out_h_; ++y)
            for (int x = 0; x < out_w_; ++x)
                for (int ch = 0; ch < 3; ++ch) {
                    int sum = 0;
                    for (int dy = 0; dy < SS; ++dy)
                        for (int dx = 0; dx < SS; ++dx) sum += px_[idx(x * SS + dx, y * SS + dy) + static_cast<std::size_t>(ch)];
                    out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w_) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(ch)] =
                        static_cast<std::uint8_t>((sum + SS * SS / 2) / (SS * SS));
                }
        return out;
    }
    void fill_rect(double x, double y, double w, double h, Rgb c, double alpha) override {
        rect_ss(x * SS, y * SS, (x + w) * SS, (y + h) * SS, c, alpha);
    }
    void polyline(const std::vector<Pt>& pts, Rgb c, double alpha, double width) override {
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) segment(pts[i].x * SS, pts[i].y * SS, pts[i + 1].x * SS, pts[i + 1].y * SS, c, alpha, width * SS / 2);
    }
    void circle(double x, double y, double r, Rgb c, double alpha) override { segment(x * SS, y * SS, x * SS, y * SS, c, alpha, r * SS); }
    void text(double x, double y, const std::string& s, double px, Rgb c, Anchor a, bool up) override {
        const double sc = px / 10.0 * SS;
        std::vector<char> text(s.begin(), s.end());
        text.push_back('\0');
        std::vector<char> buf(2048 * (s.size() + 1) + 256);
        const int quads = stb_easy_font_print(0, 0, text.data(), nullptr, buf.data(), static_cast<int>(buf.size()));
        const double width = stb_easy_font_width(text.data()) * sc;
        const double x0 = a == Anchor::Start ? 0 : a == Anchor::Middle ? -width / 2 : -width;
        constexpr double CAP = 7.0; // Glyph rows above the baseline.
        struct V {
            float x, y, z;
            unsigned char col[4];
        };
        const auto* v = reinterpret_cast<const V*>(buf.data());
        for (int q = 0; q < quads; ++q) {
            double lx[2] = {1e30, -1e30}, ly[2] = {1e30, -1e30};
            for (int i = 0; i < 4; ++i) {
                const double ux = x0 + v[q * 4 + i].x * sc, uy = (v[q * 4 + i].y - CAP) * sc;
                lx[0] = std::min(lx[0], ux); lx[1] = std::max(lx[1], ux);
                ly[0] = std::min(ly[0], uy); ly[1] = std::max(ly[1], uy);
            }
            if (up) rect_ss(x * SS + ly[0], y * SS - lx[1], x * SS + ly[1], y * SS - lx[0], c, 1);
            else rect_ss(x * SS + lx[0], y * SS + ly[0], x * SS + lx[1], y * SS + ly[1], c, 1);
        }
    }
    double text_width(const std::string& s, double px) override {
        std::vector<char> text(s.begin(), s.end());
        text.push_back('\0');
        return stb_easy_font_width(text.data()) * px / 10.0;
    }
    void clip(double x, double y, double w, double h) override {
        cx0_ = std::max(0, static_cast<int>(std::floor(x * SS)));
        cy0_ = std::max(0, static_cast<int>(std::floor(y * SS)));
        cx1_ = std::min(w_, static_cast<int>(std::ceil((x + w) * SS)));
        cy1_ = std::min(h_, static_cast<int>(std::ceil((y + h) * SS)));
    }
    void unclip() override {
        cx0_ = cy0_ = 0;
        cx1_ = w_;
        cy1_ = h_;
    }

private:
    std::size_t idx(int x, int y) const { return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)) * 3; }
    void blend(int x, int y, Rgb c, double a) {
        if (a <= 0) return;
        auto* p = &px_[idx(x, y)];
        p[0] = static_cast<std::uint8_t>(std::lround(p[0] + (c.r - p[0]) * a));
        p[1] = static_cast<std::uint8_t>(std::lround(p[1] + (c.g - p[1]) * a));
        p[2] = static_cast<std::uint8_t>(std::lround(p[2] + (c.b - p[2]) * a));
    }
    // Area-coverage fill of an axis-aligned rectangle (supersampled coordinates).
    void rect_ss(double x0, double y0, double x1, double y1, Rgb c, double alpha) {
        if (x1 < x0) std::swap(x0, x1);
        if (y1 < y0) std::swap(y0, y1);
        const int ix0 = std::max(cx0_, static_cast<int>(std::floor(x0))), ix1 = std::min(cx1_, static_cast<int>(std::ceil(x1)));
        const int iy0 = std::max(cy0_, static_cast<int>(std::floor(y0))), iy1 = std::min(cy1_, static_cast<int>(std::ceil(y1)));
        for (int y = iy0; y < iy1; ++y) {
            const double cy = std::min(y + 1.0, y1) - std::max<double>(y, y0);
            for (int x = ix0; x < ix1; ++x) {
                const double cx = std::min(x + 1.0, x1) - std::max<double>(x, x0);
                blend(x, y, c, alpha * std::clamp(cx, 0.0, 1.0) * std::clamp(cy, 0.0, 1.0));
            }
        }
    }
    // Capsule: every pixel within `r` of the segment, antialiased at the edge.
    void segment(double x0, double y0, double x1, double y1, Rgb c, double alpha, double r) {
        const double m = r + 1;
        const int ix0 = std::max(cx0_, static_cast<int>(std::floor(std::min(x0, x1) - m))), ix1 = std::min(cx1_, static_cast<int>(std::ceil(std::max(x0, x1) + m)));
        const int iy0 = std::max(cy0_, static_cast<int>(std::floor(std::min(y0, y1) - m))), iy1 = std::min(cy1_, static_cast<int>(std::ceil(std::max(y0, y1) + m)));
        const double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
        for (int y = iy0; y < iy1; ++y)
            for (int x = ix0; x < ix1; ++x) {
                const double qx = x + 0.5 - x0, qy = y + 0.5 - y0;
                const double t = len2 > 0 ? std::clamp((qx * dx + qy * dy) / len2, 0.0, 1.0) : 0.0;
                const double d = std::hypot(qx - t * dx, qy - t * dy);
                blend(x, y, c, alpha * std::clamp(r + 0.5 - d, 0.0, 1.0));
            }
    }
    int w_, h_, out_w_, out_h_;
    std::vector<std::uint8_t> px_;
    int cx0_, cy0_, cx1_, cy1_;
};
} // namespace

std::vector<double> nice_ticks(double lo, double hi, int target_count) {
    std::vector<double> ticks;
    if (!(hi > lo) || !std::isfinite(lo) || !std::isfinite(hi) || target_count < 1) return ticks;
    const double raw = (hi - lo) / target_count;
    const double mag = pow10i(static_cast<int>(std::floor(std::log10(raw))));
    const double norm = raw / mag;
    const double step = (norm <= 1.5 ? 1 : norm <= 3 ? 2 : norm <= 7 ? 5 : 10) * mag;
    const double eps = step * 1e-9;
    for (double t = std::ceil(lo / step - 1e-9) * step; t <= hi + eps && ticks.size() < 64; t += step)
        ticks.push_back(std::abs(t) < step * 1e-9 ? 0.0 : t);
    return ticks;
}

std::string format_tick(double value, double step) {
    const double a = std::abs(value);
    if (a < std::abs(step) * 1e-6 || a == 0) return "0";
    char buf[48];
    if (a >= 1e6 || a < 1e-3) {
        const int vexp = static_cast<int>(std::floor(std::log10(a))), sexp = static_cast<int>(std::floor(std::log10(std::abs(step)) + 1e-9));
        std::snprintf(buf, sizeof buf, "%.*e", std::clamp(vexp - sexp, 0, 6), value);
        std::string s = buf;
        const auto e = s.find('e');
        const int exponent = std::atoi(s.c_str() + e + 1);
        return s.substr(0, e) + "e" + std::to_string(exponent);
    }
    int decimals = 0; // Fewest decimals that still show the step exactly.
    while (decimals < 8 && std::abs(std::abs(step) * pow10i(decimals) - std::round(std::abs(step) * pow10i(decimals))) > 1e-6 * std::abs(step) * pow10i(decimals))
        ++decimals;
    std::snprintf(buf, sizeof buf, "%.*f", decimals, value);
    std::string s = buf;
    if (s.find_first_not_of("-0.") == std::string::npos) return "0";
    return s;
}

std::string render_svg(const Figure& figure, const ImageStyle& style) {
    validate(figure, style);
    SvgCanvas canvas(style, figure.title);
    draw_figure(canvas, figure, style);
    return canvas.finish();
}

std::vector<std::uint8_t> render_rgb(const Figure& figure, const ImageStyle& style) {
    validate(figure, style);
    RasterCanvas canvas(style);
    draw_figure(canvas, figure, style);
    return canvas.finish();
}

std::vector<std::uint8_t> render_png(const Figure& figure, const ImageStyle& style) {
    const auto rgb = render_rgb(figure, style);
    std::vector<std::uint8_t> png;
    const auto sink = [](void* context, void* data, int size) {
        auto* out = static_cast<std::vector<std::uint8_t>*>(context);
        out->insert(out->end(), static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);
    };
    if (!stbi_write_png_to_func(sink, &png, style.width, style.height, 3, rgb.data(), style.width * 3))
        throw std::runtime_error("PNG encoding failed");
    return png;
}

void export_figure(const std::filesystem::path& destination, const Figure& figure, ImageFormat format, const ImageStyle& style,
                   bool overwrite) {
    if (destination.empty()) throw std::invalid_argument("Image destination is empty");
    // Render first so a bad figure never leaves an empty file behind.
    std::vector<std::uint8_t> bytes;
    if (format == ImageFormat::PNG) bytes = render_png(figure, style);
    else {
        const auto svg = render_svg(figure, style);
        bytes.assign(svg.begin(), svg.end());
    }
    if (!overwrite && std::filesystem::exists(destination))
        throw std::runtime_error("Image destination already exists; confirm overwrite");
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(destination, std::ios::binary | (overwrite ? std::ios::trunc : std::ios::noreplace));
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
}
}
