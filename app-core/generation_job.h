/**
 * @file    generation_job.h
 * @brief   Runs one signal generation at a time on a background thread for the GUI.
 */

#ifndef SIGGEN_GENERATION_JOB_H
#define SIGGEN_GENERATION_JOB_H

#include "generator.h"
#include <functional>
#include <future>
#include <memory>

namespace Core {

/**
 * @class   GenerationJob
 * @brief   Asynchronous generation with a snapshot of the last completed result.
 * @details Call Start(), Poll() and Result() only on the owning (UI) thread. The worker owns its copy of the
 *          configuration.
 */
class GenerationJob {
public:
    using Worker = std::function<Core::GeneratedSignal(const Core::GenerationConfig &)>;

    explicit GenerationJob(Worker worker = Core::Generate);
    ~GenerationJob();

    bool Busy() const;
    bool Start(Core::GenerationConfig config);
    bool Poll();
    const std::shared_ptr<const Core::GeneratedSignal> &Result() const;

private:
    Worker m_Worker;
    std::future<Core::GeneratedSignal> m_Pending;
    std::shared_ptr<const Core::GeneratedSignal> m_Result;
};

} // namespace Core

#endif // SIGGEN_GENERATION_JOB_H
