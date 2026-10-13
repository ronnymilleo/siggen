/**
 * @file    generation_job.cpp
 * @brief   Runs one signal generation at a time on a background thread for the GUI.
 */

#include "generation_job.h"

#include <chrono>
#include <system_error>
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
 * @return  False, without starting anything, when a generation is already pending or the thread cannot start;
 *          Error() tells which.
 */
bool GenerationJob::Start(Core::GenerationConfig config) {
    if (Busy()) {
        return false;
    }
    m_Error.clear();
    // std::async reports a thread-creation failure with an exception; the standard library has no other channel
    try {
        m_Pending =
            std::async(std::launch::async, [config = std::move(config), worker = m_Worker] { return worker(config); });
    } catch (const std::system_error &error) {
        m_Error = error.what();
        return false;
    }
    return true;
}

/**
 * @brief   Collects the pending generation if it has finished, without blocking.
 * @return  True when a generation finished or failed since the last call; Result() or Error() then hold its
 *          outcome.
 * @note    A failed generation keeps the previous result and records the message for Error().
 */
bool GenerationJob::Poll() {
    if (!Busy() || m_Pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    auto outcome = m_Pending.get();
    if (outcome.has_value()) {
        m_Result = std::make_shared<const Core::GeneratedSignal>(std::move(*outcome));
    } else {
        m_Error = std::move(outcome).error();
    }
    return true;
}

/**
 * @brief   Returns the last completed signal.
 * @return  An immutable snapshot that stays valid after later generations, or null before the first one.
 */
const std::shared_ptr<const Core::GeneratedSignal> &GenerationJob::Result() const {
    return m_Result;
}

/**
 * @brief   Returns why the last generation failed to start or finish.
 * @return  The message, or an empty string.
 */
const std::string &GenerationJob::Error() const {
    return m_Error;
}

} // namespace Core
