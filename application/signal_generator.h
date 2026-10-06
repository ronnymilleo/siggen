#pragma once
#include "generation_job.h"
#include "imgui_window_layer.h"
#include "iq_export.h"
#include "measurements.h"
#include "pipeline.h"
#include "plot_data.h"
#include <optional>
#include <string>

class SignalGenerator : public ImGuiWindowLayer
{
public:
    explicit SignalGenerator(iq::GenerationConfig config = {});
    ~SignalGenerator() override = default;

protected:
    void DrawContents() override;

private:
    friend struct SignalGeneratorTestAccess;
    iq::GenerationConfig                       config_;
    std::vector<char>                          bit_input_;
    GenerationJob                              job_;
    std::string                                error_;
    PlotData                                   plots_;
    iq::Spectrum                               spectrum_;
    std::vector<double>                        spectrum_db_;
    bool                                       waveform_fit_       = true;
    bool                                       spectrum_fit_       = true;
    char                                       preset_path_[1024]  = "default.preset";
    int                                        constellation_view_ = 0;
    iq::Window                                 window_             = iq::Window::Hann;
    iq::PowerStatistics                        power_;
    std::optional<iq::SymbolAccuracy>          accuracy_;
    iq::EyeDiagram                             eye_;
    int                                        eye_component_ = 0;
    std::optional<iq::PipelineStages>          pipeline_;
    int                                        pipeline_first_ = 0;
    int                                        pipeline_count_ = 12;
    bool                                       pipeline_align_ = true;
    bool                                       pipeline_fit_   = true;
    std::string                                preset_notes_;
    void                                       UpdateSpectrum();
    void                                       DrawMeasurements(const iq::GeneratedSignal& result);
    void                                       DrawPlots();
    void                                       DrawPipeline(const iq::GeneratedSignal& result);
    void                                       DrawExportDialog();
    void                                       Export(bool overwrite);
    char                                       export_path_[1024] = "signal.csv";
    int                                        export_format_     = 0;
    std::shared_ptr<const iq::GeneratedSignal> export_result_;
    std::string                                export_status_;
    bool                                       confirm_overwrite_ = false;
};
