#pragma once

#include "common/platform_config.h"

#include <memory>

namespace crow {
template <typename... Middlewares>
class Crow;
}

namespace oj::worker {

class JudgeCore;

struct WorkerAppContext {
    oj::common::ServiceConfig config{};
    std::shared_ptr<JudgeCore> judge_core;
};

void register_routes(crow::Crow<>& app, const WorkerAppContext& context);

} // namespace oj::worker
