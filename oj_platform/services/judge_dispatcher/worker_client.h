#pragma once

#include "common/platform_config.h"
#include "common/protocol.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

namespace oj::dispatcher {

class WorkerClient {
public:
    explicit WorkerClient(oj::common::JudgeWorkerEndpoint endpoint = {});

    oj::protocol::JudgeResponse judge(const oj::protocol::JudgeRequest& request) const;

private:
    oj::common::JudgeWorkerEndpoint endpoint_;
};

class WorkerPool {
public:
    explicit WorkerPool(std::vector<oj::common::JudgeWorkerEndpoint> endpoints,
                        std::chrono::milliseconds cooldown = std::chrono::seconds(10));

    oj::protocol::JudgeResponse judge(const oj::protocol::JudgeRequest& request);
    std::size_t size() const noexcept;

private:
    struct WorkerState {
        oj::common::JudgeWorkerEndpoint endpoint;
        std::chrono::steady_clock::time_point unavailable_until{};
    };

    std::vector<WorkerState> workers_;
    std::size_t next_index_{0};
    std::chrono::milliseconds cooldown_;
};

} // namespace oj::dispatcher