/**
 * @file    ber_job.h
 * @brief   Runs a bit error rate sweep in the background, with progress and cancellation.
 */

#ifndef SIGGEN_BER_JOB_H
#define SIGGEN_BER_JOB_H

#include "ber.h"
#include "generator.h"
#include <atomic>
#include <future>
#include <string>
#include <vector>

namespace Core {

/**
 * @class   BerJob
 * @brief   Owns one BER sweep running on a worker thread.
 * @details Call every method from the owning (UI) thread only. The worker sees its own copies of the configuration
 *          and settings and communicates through the progress and cancel atomics. Destruction cancels the sweep and
 *          waits for the worker.
 */
class BerJob {
public:
    BerJob() = default;
    ~BerJob();
    BerJob(const BerJob &) = delete;
    BerJob &operator=(const BerJob &) = delete;

    // Running
    bool Start(GenerationConfig config, BerSweepSettings settings);
    void Cancel();
    bool Poll();
    bool Busy() const;
    int Total() const;
    int Done() const;

    // Results
    const std::vector<BerPoint> &Points() const;
    const GenerationConfig &Config() const;
    const std::string &Error() const;
    void Clear();

private:
    std::future<std::vector<BerPoint>> m_Pending;
    std::atomic<bool> m_Cancel{false};
    std::atomic<int> m_Done{0};
    int m_Total = 0;
    GenerationConfig m_Config;
    std::vector<BerPoint> m_Points;
    std::string m_Error;
};

} // namespace Core

#endif // SIGGEN_BER_JOB_H
