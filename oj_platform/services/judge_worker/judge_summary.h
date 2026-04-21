#pragma once

#include "common/protocol.hpp"

namespace oj::worker {

void summarize_judge_response(oj::protocol::JudgeResponse& response);

} // namespace oj::worker