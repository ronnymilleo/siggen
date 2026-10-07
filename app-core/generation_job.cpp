/**
 * @file    generation_job.cpp
 * @brief   Runs one signal generation at a time on a background thread for the GUI.
 */

#include "generation_job.h"

#include <chrono>
#include <utility>

namespace Core {

/**
 * @brief   Creates an idle job.
 * @param[in] worker    Function that generates a signal from a configuration; Core::Generate by default.
 */
GenerationJob::GenerationJob(Worker worker) : m_Worker(std::move(worker)) {
}

/**
 * @brief   Waits for a running worker to finish before the job goes away.
 */
GenerationJob::~GenerationJob() {
    if (m_Pending.valid()) {
        m_Pending.wait();
    }
}

/**
 * @brief   Tells whether a generation is running or finished but not yet collected by Poll().
 * @return  True while a worker result is pending.
 */
bool GenerationJob::Busy() const {
    return m_Pending.valid();
}

/**
 * @brief   Starts a generation on a background thread.
 * @param[in] config    Configuration, moved into the worker.
 * @return  False, without starting anything, when a generation is already pending.
 */
bool GenerationJob::Start(Core::GenerationConfig config) {
    if (Busy()) {
        return false;
    }
    m_Pending =
        std::async(std::launch::async, [config = std::move(config), worker = m_Worker] { return worker(config); });
    return true;
}

/**
 * @brief   Collects the pending generation if it has finished, without blocking.
 * @return  True when a new result replaced the previous one.
 * @note    Rethrows the exception of a failed worker; the job is idle afterwards and the previous result stays.
 */
bool GenerationJob::Poll() {
    if (!Busy() || m_Pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    auto completed = std::make_shared<const Core::GeneratedSignal>(m_Pending.get());
    m_Result = std::move(completed);
    return true;
}

/**
 * @brief   Returns the last completed signal.
 * @return  An immutable snapshot that stays valid after later generations, or null before the first one.
 */
const std::shared_ptr<const Core::GeneratedSignal> &GenerationJob::Result() const {
    return m_Result;
}

} // namespace Core
