#include "box/ecs/system_scheduler.hpp"

#include <algorithm>
#include <exception>
#include <future>
#include <utility>

namespace box::ecs {

bool SystemScheduler::addSystem(std::string name, std::int32_t order,
                                SystemExecution execution,
                                std::function<void(double)> update) {
    if (name.empty() || !update ||
        std::any_of(systems_.begin(), systems_.end(), [&name](const SystemRegistration& system) {
            return system.name == name;
        })) {
        return false;
    }
    systems_.push_back({std::move(name), order, execution, std::move(update),
                        nextRegistrationIndex_++});
    std::stable_sort(systems_.begin(), systems_.end(),
                     [](const SystemRegistration& lhs, const SystemRegistration& rhs) {
        if (lhs.order != rhs.order) return lhs.order < rhs.order;
        return lhs.registrationIndex < rhs.registrationIndex;
    });
    return true;
}

bool SystemScheduler::removeSystem(const std::string& name) {
    const auto found = std::find_if(systems_.begin(), systems_.end(), [&name](const auto& system) {
        return system.name == name;
    });
    if (found == systems_.end()) return false;
    systems_.erase(found);
    return true;
}

void SystemScheduler::run(double deltaSeconds) {
    std::size_t groupStart = 0;
    while (groupStart < systems_.size()) {
        std::size_t groupEnd = groupStart + 1U;
        while (groupEnd < systems_.size() && systems_[groupEnd].order == systems_[groupStart].order) {
            ++groupEnd;
        }

        std::vector<std::future<void>> backgroundJobs;
        std::exception_ptr firstException;
        for (std::size_t index = groupStart; index < groupEnd; ++index) {
            const SystemRegistration& system = systems_[index];
            if (system.execution == SystemExecution::WorkerPool) {
                try {
                    const auto update = system.update;
                    backgroundJobs.push_back(jobs_.submit([update, deltaSeconds]() {
                        update(deltaSeconds);
                    }));
                } catch (...) {
                    firstException = std::current_exception();
                    break;
                }
            } else {
                try {
                    system.update(deltaSeconds);
                } catch (...) {
                    firstException = std::current_exception();
                    break;
                }
            }
        }

        for (std::future<void>& job : backgroundJobs) {
            try {
                job.get();
            } catch (...) {
                if (firstException == nullptr) firstException = std::current_exception();
            }
        }
        if (firstException != nullptr) std::rethrow_exception(firstException);
        groupStart = groupEnd;
    }
}

}  // namespace box::ecs
