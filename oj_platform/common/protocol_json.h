#pragma once

#include "common/protocol.hpp"

#include <string>

namespace oj::common {

std::string serialize_judge_request(const oj::protocol::JudgeRequest& request);
oj::protocol::JudgeRequest deserialize_judge_request(const std::string& payload);

std::string serialize_judge_response(const oj::protocol::JudgeResponse& response);
oj::protocol::JudgeResponse deserialize_judge_response(const std::string& payload);

} // namespace oj::common