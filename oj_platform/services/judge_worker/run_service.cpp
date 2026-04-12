#include "services/judge_worker/run_service.h"

namespace oj::worker {

std::string RunService::run(const std::string& executable_path) const {
    return "executed " + executable_path + " successfully";
}

} // namespace oj::worker

