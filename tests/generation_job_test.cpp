/**
 * @file    generation_job_test.cpp
 * @brief   Tests for the background generation job: snapshots, repeated runs, failures and shutdown.
 */

#include "generation_job.h"

#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

namespace Core {

namespace {

// Polls the job until it is idle; a failed generation rethrows its exception here
void Finish(GenerationJob &job) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (job.Busy()) {
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("Job timeout");
        }
        job.Poll();
        std::this_thread::yield();
    }
}

} // namespace

TEST(GenerationJob, SnapshotRepeatedGenerationAndFailure) {
    GenerationJob job;
    GenerationConfig config;
    ASSERT_TRUE(job.Start(config));
    config.Seed = 7;
    ASSERT_FALSE(job.Start(config));
    Finish(job);
    auto previous = job.Result();
    ASSERT_TRUE(previous);
    EXPECT_EQ(previous->Config.Seed, 5489u);
    EXPECT_EQ(previous->Samples, Generate({}).Samples);
    ASSERT_TRUE(job.Start(config));
    EXPECT_EQ(job.Result(), previous);
    Finish(job);
    EXPECT_EQ(job.Result()->Config.Seed, 7u);
    previous = job.Result();
    config.SymbolCount = 0;
    ASSERT_TRUE(job.Start(config));
    EXPECT_THROW(Finish(job), std::invalid_argument);
    EXPECT_FALSE(job.Busy());
    EXPECT_EQ(job.Result(), previous);
}

TEST(GenerationJob, DestructionWaitsForOwnedWorker) {
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::atomic<bool> completed = false;
    auto owner = std::async(std::launch::async, [&] {
        GenerationJob job([&](const GenerationConfig &config) {
            entered.set_value();
            released.wait();
            completed = true;
            return Generate(config);
        });
        job.Start({});
    });
    entered.get_future().wait();
    EXPECT_EQ(owner.wait_for(std::chrono::milliseconds(10)), std::future_status::timeout);
    release.set_value();
    owner.get();
    EXPECT_TRUE(completed);
}

} // namespace Core
