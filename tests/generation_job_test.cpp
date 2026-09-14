#include "generation_job.h"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
namespace {
void finish(GenerationJob& job) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (job.busy()) {
        if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("Job timeout");
        job.poll();
        std::this_thread::yield();
    }
}
}
TEST(GenerationJob, SnapshotRepeatedGenerationAndFailure) {
    GenerationJob job;
    iq::GenerationConfig config;
    ASSERT_TRUE(job.start(config));
    config.seed = 7;
    ASSERT_FALSE(job.start(config));
    finish(job);
    auto previous = job.result();
    ASSERT_TRUE(previous);
    EXPECT_EQ(previous->config.seed, 5489u);
    EXPECT_EQ(previous->samples, iq::generate({}).samples);
    ASSERT_TRUE(job.start(config));
    EXPECT_EQ(job.result(), previous);
    finish(job);
    EXPECT_EQ(job.result()->config.seed, 7u);
    previous = job.result();
    config.symbol_count = 0;
    ASSERT_TRUE(job.start(config));
    EXPECT_THROW(finish(job), std::invalid_argument);
    EXPECT_FALSE(job.busy());
    EXPECT_EQ(job.result(), previous);
}
TEST(GenerationJob, DestructionWaitsForOwnedWorker) {
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<bool> completed = false;
    auto owner = std::async(std::launch::async, [&] {
        GenerationJob job([&](const iq::GenerationConfig& c) {
            entered.set_value(); released.wait(); completed = true; return iq::generate(c);
        });
        job.start({});
    });
    entered.get_future().wait();
    EXPECT_EQ(owner.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    release.set_value();
    owner.get();
    EXPECT_TRUE(completed);
}
