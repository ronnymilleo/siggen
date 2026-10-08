/**
 * @file    summary_window.h
 * @brief   The Signal Summary window: what was generated and how it measures.
 */

#ifndef SIGGEN_SUMMARY_WINDOW_H
#define SIGGEN_SUMMARY_WINDOW_H

#include "app_window.h"
#include "generator_session.h"

namespace GUI {

/**
 * @class   SummaryWindow
 * @brief   Shows the parameters, noise and impairment record, measurements and bit errors of the last generated
 *          signal.
 * @details Warns when the settings changed since that signal was generated. Holds no state of its own.
 */
class SummaryWindow : public AppWindow {
public:
    explicit SummaryWindow(const GeneratorSession &session);

private:
    void Draw() override;

    void DrawNoiseSummary(const Core::GeneratedSignal &result);
    void DrawSymbolSummary(const Core::GeneratedSignal &result);
    void DrawMeasurements(const Core::GeneratedSignal &result);
    void DrawBitErrors(const Core::GeneratedSignal &result);

    const GeneratorSession &m_Session;
};

} // namespace GUI

#endif // SIGGEN_SUMMARY_WINDOW_H
