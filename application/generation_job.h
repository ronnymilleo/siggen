#pragma once
#include "generator.h"
#include <chrono>
#include <functional>
#include <future>
#include <memory>

// Call start/poll/result only on the owning (UI) thread. The worker owns its config.
class GenerationJob {
public:
    using Worker = std::function<iq::GeneratedSignal(const iq::GenerationConfig&)>;
    explicit GenerationJob(Worker worker = iq::generate) : worker_(std::move(worker)) {}
    ~GenerationJob() { if (pending_.valid()) pending_.wait(); }
    bool busy() const { return pending_.valid(); }
    bool start(iq::GenerationConfig config) {
        if (busy()) return false;
        pending_ = std::async(std::launch::async, [config = std::move(config), worker = worker_] { return worker(config); });
        return true;
    }
    bool poll() {
        if (!busy() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        auto completed = std::make_shared<const iq::GeneratedSignal>(pending_.get());
        result_ = std::move(completed);
        return true;
    }
    const std::shared_ptr<const iq::GeneratedSignal>& result() const { return result_; }
private:
    Worker worker_;
    std::future<iq::GeneratedSignal> pending_;
    std::shared_ptr<const iq::GeneratedSignal> result_;
};
