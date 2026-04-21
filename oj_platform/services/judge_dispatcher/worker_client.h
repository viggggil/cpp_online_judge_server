#pragma once

#include "common/platform_config.h"
#include "common/protocol.hpp"

namespace oj::dispatcher {

class WorkerClient {
public:
    explicit WorkerClient(oj::common::JudgeWorkerEndpoint endpoint = {});

    oj::protocol::JudgeResponse judge(const oj::protocol::JudgeRequest& request) const;

private:
    oj::common::JudgeWorkerEndpoint endpoint_;
};

} // namespace oj::dispatcher