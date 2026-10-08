/**
 * @file    plot_data.h
 * @brief   Decimated sample, constellation and frequency series that the GUI and image export plot.
 */

#ifndef SIGGEN_PLOT_DATA_H
#define SIGGEN_PLOT_DATA_H

#include "signal_analysis.h"
#include <cstddef>
#include <vector>

namespace Core {

/**
 * @struct  XYData
 * @brief   Paired X and Y coordinates of a scatter plot.
 */
struct XYData {
    std::vector<double> X, Y;
};

/**
 * @struct  PlotData
 * @brief   Everything the plots of one generated signal need, decimated to a bounded number of points.
 */
struct PlotData {
    std::vector<double> Time, I, Q;
    XYData Mapped, Matched;
    // FSK family: frequency estimate per sample pair against the nominal tone of each symbol
    std::vector<double> FreqTime, FreqEstimate, FreqNominal;
};

PlotData MakePlotData(const Core::GeneratedSignal &signal, std::size_t limit = 4096);

} // namespace Core

#endif // SIGGEN_PLOT_DATA_H
