/**
 * @file    generation_job.h
 * @brief   Runs one signal generation at a time on a background thread for the GUI.
 */

#ifndef SIGGEN_GENERATION_JOB_H
#define SIGGEN_GENERATION_JOB_H

#include "generator.h"
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <string>

namespace Core {

/**
 * @class   GenerationJob
 * @brief   Asynchronous generation with a snapshot of the last completed result.
 * @details Call Start(), Poll() and Result() only on the owning (UI) thread. The worker owns its copy of the
 *          configuration. A failed generation keeps its message for Error() and the previous result stays.
 */
class GenerationJob {
public:
    using Worker = std::function<std::expected<Core::GeneratedSignal, std::string>(const Core::GenerationConfig &)>;

    explicit GenerationJob(Worker worker = Core::Generate);
    ~GenerationJob();

    bool Busy() const;
    bool Start(Core::GenerationConfig config);
    bool Poll();
    const std::shared_ptr<const Core::GeneratedSignal> &Result() const;
    const std::string &Error() const;

private:
    Worker m_Worker;
    std::future<std::expected<Core::GeneratedSignal, std::string>> m_Pending;
    std::shared_ptr<const Core::GeneratedSignal> m_Result;
    std::string m_Error;
};

} // namespace Core

#endif // SIGGEN_GENERATION_JOB_H
