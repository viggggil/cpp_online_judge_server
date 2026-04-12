#include "services/judge_worker/compile_service.h"

namespace oj::worker {

std::string CompileService::compile(const std::string& language, const std::string&) const {
    return "/tmp/fake_artifact_" + language;
}

} // namespace oj::worker

