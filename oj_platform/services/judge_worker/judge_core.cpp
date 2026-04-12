#include "services/judge_worker/judge_core.h"

#include "services/judge_worker/compile_service.h"
#include "services/judge_worker/run_service.h"

namespace oj::worker {

oj::common::SubmissionResult JudgeCore::judge(const oj::common::SubmissionRequest& request) const {
    CompileService compile_service;
    RunService run_service;

    const auto artifact = compile_service.compile(request.language, request.source_code);
    const auto execution_result = run_service.run(artifact);

    return {
        "worker-local",
        "accepted",
        "judge result for problem " + request.problem_id + ": " + execution_result};
}

} // namespace oj::worker

