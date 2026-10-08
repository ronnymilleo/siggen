/**
 * @file    ber_job.cpp
 * @brief   Runs a bit error rate sweep in the background, with progress and cancellation.
 */

#include "ber_job.h"

#include <chrono>
#include <exception>
#include <utility>

namespace Core {

/**
 * @brief   Cancels a running sweep and waits for its worker to stop.
 */
BerJob::~BerJob() {
    m_Cancel.store(true);
    if (m_Pending.valid()) {
        m_Pending.wait();
    }
}

/**
 * @brief   Starts a sweep on a worker thread.
 * @param[in] config    Waveform, pulse, rate, gain and impairments of the sweep.
 * @param[in] settings  Eb/N0 points and stopping rules.
 * @return  False, without starting anything, when a sweep is already running.
 * @note    Clears the error of the previous sweep; its points stay until Poll() collects the new ones.
 */
bool BerJob::Start(GenerationConfig config, BerSweepSettings settings) {
    if (Busy()) {
        return false;
    }
    m_Total = static_cast<int>(settings.EbN0Db.size());
    m_Done.store(0);
    m_Cancel.store(false);
    m_Config = config;
    m_Error.clear();
    m_Pending = std::async(std::launch::async, [this, config = std::move(config), settings = std::move(settings)] {
        return BerSweep(config, settings, &m_Cancel, &m_Done);
    });
    return true;
}

/**
 * @brief   Asks the running sweep to stop; it keeps the points finished so far.
 */
void BerJob::Cancel() {
    m_Cancel.store(true);
}

/**
 * @brief   Collects the sweep if it has finished, without blocking.
 * @return  True when a sweep finished or failed since the last call; Points() or Error() then hold its outcome.
 */
bool BerJob::Poll() {
    if (!Busy() || m_Pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    try {
        m_Points = m_Pending.get();
    } catch (const std::exception &error) {
        m_Pending = {};
        m_Error = error.what();
    }
    return true;
}

/**
 * @brief   Tells whether a sweep is running or finished but not yet collected.
 * @return  True until Poll() collects the sweep.
 */
bool BerJob::Busy() const {
    return m_Pending.valid();
}

/**
 * @brief   Returns the number of Eb/N0 points of the current or last sweep.
 * @return  The point count.
 */
int BerJob::Total() const {
    return m_Total;
}

/**
 * @brief   Returns how many points the current sweep has finished.
 * @return  The count, updated by the worker.
 */
int BerJob::Done() const {
    return m_Done.load();
}

/**
 * @brief   Returns the points of the last collected sweep.
 * @return  One point per finished Eb/N0 value, in order.
 */
const std::vector<BerPoint> &BerJob::Points() const {
    return m_Points;
}

/**
 * @brief   Returns the configuration of the latest sweep.
 * @return  The configuration passed to the last Start().
 */
const GenerationConfig &BerJob::Config() const {
    return m_Config;
}

/**
 * @brief   Returns why the last sweep failed.
 * @return  The message, or an empty string.
 */
const std::string &BerJob::Error() const {
    return m_Error;
}

/**
 * @brief   Forgets the points and error of the last sweep.
 */
void BerJob::Clear() {
    m_Points.clear();
    m_Error.clear();
}

} // namespace Core
