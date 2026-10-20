#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "box/ecs/job_system.hpp"

namespace box::ecs {

enum class SystemExecution { MainThread, WorkerPool };

struct SystemRegistration {
    std::string name;
    std::int32_t order = 0;
    SystemExecution execution = SystemExecution::MainThread;
    std::function<void(double)> update;
    std::uint64_t registrationIndex = 0;
};

/** Runs same-order worker jobs in parallel, then joins before advancing to later system phases. */
class SystemScheduler final {
public:
    explicit SystemScheduler(std::size_t workerCount = 0U) : jobs_(workerCount) {}

    [[nodiscard]] bool addSystem(std::string name, std::int32_t order,
                                 SystemExecution execution,
                                 std::function<void(double)> update);
    [[nodiscard]] bool removeSystem(const std::string& name);
    void run(double deltaSeconds);
    [[nodiscard]] std::size_t systemCount() const noexcept { return systems_.size(); }
    [[nodiscard]] JobSystem& jobs() noexcept { return jobs_; }

private:
    JobSystem jobs_;
    std::vector<SystemRegistration> systems_;
    std::uint64_t nextRegistrationIndex_ = 0;
};

}  // namespace box::ecs
