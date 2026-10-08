/**
 * @file    plot_figures.h
 * @brief   Figures for image export, built from the same data the GUI plots.
 */

#ifndef SIGGEN_PLOT_FIGURES_H
#define SIGGEN_PLOT_FIGURES_H

#include "ber.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include "plot_export.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Core {

// Colours match the screen: I blue, Q orange
inline constexpr Core::Rgb InPhaseColor{51, 153, 255}, QuadratureColor{255, 140, 38};

/**
 * @struct  BerPlot
 * @brief   A BER curve split for plotting: points that saw errors, points that saw none and the textbook curve.
 */
struct BerPlot {
    std::vector<double> X, Y;             // Measured, with errors
    std::vector<double> BoundX, BoundY;   // No error seen: BER below 1 / bits, drawn at that upper bound
    std::vector<double> TheoryX, TheoryY; // Textbook curve over the swept range, where one exists
};

Core::Series MakeLineSeries(std::string label, const std::vector<double> &x, const std::vector<double> &y,
                            Core::Rgb colour, double width = 2.0, double alpha = 1.0);

Core::Figure WaveformFigure(const PlotData &plot);
Core::Figure FrequencyFigure(const PlotData &plot);
Core::Figure ConstellationFigure(const XYData &data, std::string title);
Core::Figure EyeFigure(const Core::EyeDiagram &eye, bool quadrature);
Core::Figure SpectrumFigure(const Core::Spectrum &spectrum, const std::vector<double> &db);
Core::Figure PipelineFigure(const Core::PipelineStages &stages, int first, int count, bool align);

BerPlot MakeBerPlot(const std::vector<BerPoint> &points, Modulation modulation);
double LowestResolvableBer(const std::vector<BerPoint> &points);
Core::Figure BerFigure(const std::vector<BerPoint> &points, Modulation modulation,
                       std::optional<std::array<double, 2>> current);

} // namespace Core

#endif // SIGGEN_PLOT_FIGURES_H
