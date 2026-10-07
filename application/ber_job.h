#pragma once
#include "ber.h"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>

// Runs a BER sweep off the UI thread. Call start/poll/cancel/result only on the owning (UI) thread;
// the worker sees only its own copies of the configuration and the atomics.
class BerJob {
public:
    ~BerJob() { cancel_.store(true); if (pending_.valid()) pending_.wait(); }
    bool busy() const { return pending_.valid(); }
    int total() const { return total_; }
    int done() const { return done_.load(); }
    bool start(iq::GenerationConfig config, iq::BerSweepSettings settings) {
        if (busy()) return false;
        total_ = static_cast<int>(settings.eb_n0_db.size());
        done_.store(0);
        cancel_.store(false);
        config_ = config;
        error_.clear();
        pending_ = std::async(std::launch::async, [this, config = std::move(config), settings = std::move(settings)] {
            return iq::ber_sweep(config, settings, &cancel_, &done_);
        });
        return true;
    }
    void cancel() { cancel_.store(true); }
    // True when a sweep finished (or failed) since the last call.
    bool poll() {
        if (!busy() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        try {
            points_ = pending_.get();
        } catch (const std::exception& e) {
            pending_ = {};
            error_ = e.what();
        }
        return true;
    }
    const std::vector<iq::BerPoint>& points() const { return points_; }
    const iq::GenerationConfig& config() const { return config_; } // Configuration of the latest sweep.
    const std::string& error() const { return error_; }
    void clear() { points_.clear(); error_.clear(); }
private:
    std::future<std::vector<iq::BerPoint>> pending_;
    std::atomic<bool> cancel_{false};
    std::atomic<int> done_{0};
    int total_ = 0;
    iq::GenerationConfig config_;
    std::vector<iq::BerPoint> points_;
    std::string error_;
};
