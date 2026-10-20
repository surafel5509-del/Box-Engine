#include "box/ecs/job_system.hpp"

#include <algorithm>

#include "box/core/log.hpp"

namespace box::ecs {

JobSystem::JobSystem(std::size_t workerCount) {
    if (workerCount == 0U) {
        const unsigned int hardwareThreads = std::thread::hardware_concurrency();
        workerCount = hardwareThreads > 1U ? static_cast<std::size_t>(hardwareThreads - 1U) : 1U;
    }
    workerCount = std::clamp<std::size_t>(workerCount, 1U, 8U);
    workers_.reserve(workerCount);
    for (std::size_t index = 0; index < workerCount; ++index) {
        workers_.emplace_back(&JobSystem::workerLoop, this);
    }
}

JobSystem::~JobSystem() { stop(); }

bool JobSystem::enqueue(std::function<void()> job) {
    if (!job) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        jobs_.push(std::move(job));
        ++pendingJobs_;
    }
    workAvailable_.notify_one();
    return true;
}

void JobSystem::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this]() { return pendingJobs_ == 0U; });
}

void JobSystem::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    workAvailable_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void JobSystem::workerLoop() {
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            workAvailable_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
            if (stopping_ && jobs_.empty()) return;
            job = std::move(jobs_.front());
            jobs_.pop();
        }

        try {
            job();
        } catch (const std::exception& exception) {
            BOX_LOG_ERROR("Unhandled worker job exception: %s", exception.what());
        } catch (...) {
            BOX_LOG_ERROR("Unhandled unknown worker job exception.");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --pendingJobs_;
            if (pendingJobs_ == 0U) idle_.notify_all();
        }
    }
}

}  // namespace box::ecs
