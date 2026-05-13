#pragma once

#include "common/platform_config.h"

#include <cstdint>
#include <optional>
#include <string>

namespace oj::common {

struct RabbitMqDelivery {
    std::string body;
    std::uint64_t delivery_tag{};
};

class RabbitMqClient {
public:
    explicit RabbitMqClient(const RabbitMqConfig& config = {});
    ~RabbitMqClient();

    RabbitMqClient(const RabbitMqClient&) = delete;
    RabbitMqClient& operator=(const RabbitMqClient&) = delete;

    bool available() const noexcept;
    std::string last_error() const;

    void declare_topology();

    bool publish_judge_task(const std::string& body);

    std::optional<RabbitMqDelivery> consume_one(int timeout_ms);

    void ack(std::uint64_t delivery_tag);
    void nack_requeue(std::uint64_t delivery_tag);
    void reject_dead(std::uint64_t delivery_tag);

private:
    struct Impl;
    Impl* impl_{nullptr};

    void reset_consumer() noexcept;
};

} // namespace oj::common
