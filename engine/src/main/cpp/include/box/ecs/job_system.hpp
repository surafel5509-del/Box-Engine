#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace box::ecs {

/** Bounded worker pool for background asset reads and explicitly parallel engine jobs. */
class JobSystem final {
public:
    explicit JobSystem(std::size_t workerCount = 0U);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    template <typename Function>
    auto submit(Function&& function) -> std::future<std::invoke_result_t<Function>> {
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(
            static_cast<Function&&>(function));
        std::future<Result> result = task->get_future();
        if (!enqueue([task]() { (*task)(); })) {
            throw std::runtime_error("Cannot submit work to a stopped JobSystem.");
        }
        return result;
    }

    [[nodiscard]] bool enqueue(std::function<void()> job);
    void waitIdle();
    void stop();
    [[nodiscard]] std::size_t workerCount() const noexcept { return workers_.size(); }

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;
    std::queue<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    std::size_t pendingJobs_ = 0;
    bool stopping_ = false;
};

}  // namespace box::ecs
