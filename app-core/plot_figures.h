/**
 * @file    plot_figures.h
 * @brief   Figures for image export, built from the same data the GUI plots.
 */

#ifndef SIGGEN_PLOT_FIGURES_H
#define SIGGEN_PLOT_FIGURES_H

#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include "plot_export.h"
#include <string>
#include <vector>

namespace Core {

// Colours match the screen: I blue, Q orange
inline constexpr Core::Rgb InPhaseColor{51, 153, 255}, QuadratureColor{255, 140, 38};

Core::Series MakeLineSeries(std::string label, const std::vector<double> &x, const std::vector<double> &y,
                            Core::Rgb colour, double width = 2.0, double alpha = 1.0);

Core::Figure WaveformFigure(const PlotData &plot);
Core::Figure FrequencyFigure(const PlotData &plot);
Core::Figure ConstellationFigure(const XYData &data, std::string title);
Core::Figure EyeFigure(const Core::EyeDiagram &eye, bool quadrature);
Core::Figure SpectrumFigure(const Core::Spectrum &spectrum, const std::vector<double> &db);
Core::Figure PipelineFigure(const Core::PipelineStages &stages, int first, int count, bool align);

} // namespace Core

#endif // SIGGEN_PLOT_FIGURES_H
