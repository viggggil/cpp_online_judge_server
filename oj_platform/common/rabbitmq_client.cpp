#include "common/rabbitmq_client.h"

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>

#include <stdexcept>
#include <string>

namespace oj::common {

namespace {

amqp_bytes_t to_amqp_bytes(const char* text) {
    return amqp_cstring_bytes(text);
}

std::string reply_text_to_string(const amqp_bytes_t& text) {
    return std::string{
        static_cast<const char*>(text.bytes),
        static_cast<std::size_t>(text.len)};
}

void throw_on_rpc_error(const amqp_rpc_reply_t& reply, const std::string& action) {
    switch (reply.reply_type) {
    case AMQP_RESPONSE_NORMAL:
        return;
    case AMQP_RESPONSE_NONE:
        throw std::runtime_error(action + ": missing rpc reply");
    case AMQP_RESPONSE_LIBRARY_EXCEPTION:
        throw std::runtime_error(action + ": " + std::string{amqp_error_string2(reply.library_error)});
    case AMQP_RESPONSE_SERVER_EXCEPTION:
        if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD) {
            const auto* close = static_cast<amqp_connection_close_t*>(reply.reply.decoded);
            throw std::runtime_error(
                action + ": connection closed " + std::to_string(close->reply_code) +
                " " + reply_text_to_string(close->reply_text));
        }
        if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD) {
            const auto* close = static_cast<amqp_channel_close_t*>(reply.reply.decoded);
            throw std::runtime_error(
                action + ": channel closed " + std::to_string(close->reply_code) +
                " " + reply_text_to_string(close->reply_text));
        }
        throw std::runtime_error(action + ": unexpected server exception");
    }

    throw std::runtime_error(action + ": unknown rpc error");
}

bool is_timeout_reply(const amqp_rpc_reply_t& reply) {
    return reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
           reply.library_error == AMQP_STATUS_TIMEOUT;
}

bool publish_message(amqp_connection_state_t connection,
                     amqp_channel_t channel_id,
                     const char* exchange,
                     const char* routing_key,
                     const std::string& body) {
    amqp_basic_properties_t properties{};
    properties._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    properties.content_type = amqp_cstring_bytes("application/json");
    properties.delivery_mode = 2;

    const int rc = amqp_basic_publish(
        connection,
        channel_id,
        to_amqp_bytes(exchange),
        to_amqp_bytes(routing_key),
        0,
        0,
        &properties,
        amqp_bytes_t{body.size(), const_cast<char*>(body.data())});

    return rc == AMQP_STATUS_OK;
}

amqp_table_t make_retry_queue_arguments(const RabbitMqConfig& config) {
    static constexpr int kRetryArgumentCount = 3;
    auto* entries = new amqp_table_entry_t[kRetryArgumentCount];

    entries[0].key = amqp_cstring_bytes("x-message-ttl");
    entries[0].value.kind = AMQP_FIELD_KIND_I32;
    entries[0].value.value.i32 = config.retry_delay_ms;

    entries[1].key = amqp_cstring_bytes("x-dead-letter-exchange");
    entries[1].value.kind = AMQP_FIELD_KIND_UTF8;
    entries[1].value.value.bytes = amqp_cstring_bytes(config.judge_exchange);

    entries[2].key = amqp_cstring_bytes("x-dead-letter-routing-key");
    entries[2].value.kind = AMQP_FIELD_KIND_UTF8;
    entries[2].value.value.bytes = amqp_cstring_bytes(config.judge_routing_key);

    return amqp_table_t{kRetryArgumentCount, entries};
}

void destroy_table_arguments(amqp_table_t& table) {
    delete[] table.entries;
    table.entries = nullptr;
    table.num_entries = 0;
}

} // namespace

struct RabbitMqClient::Impl {
    RabbitMqConfig config;
    amqp_connection_state_t connection{nullptr};
    amqp_socket_t* socket{nullptr};
    amqp_channel_t channel_id{1};
    std::string consumer_tag;
    std::string last_error;
};

RabbitMqClient::RabbitMqClient(const RabbitMqConfig& config)
    : impl_(new Impl{}) {
    impl_->config = config;

    try {
        impl_->connection = amqp_new_connection();
        if (impl_->connection == nullptr) {
            throw std::runtime_error("failed to allocate rabbitmq connection");
        }

        impl_->socket = amqp_tcp_socket_new(impl_->connection);
        if (impl_->socket == nullptr) {
            throw std::runtime_error("failed to create rabbitmq tcp socket");
        }

        const int socket_status = amqp_socket_open(impl_->socket, config.host, config.port);
        if (socket_status != AMQP_STATUS_OK) {
            throw std::runtime_error(
                std::string{"failed to connect rabbitmq: "} + amqp_error_string2(socket_status));
        }

        throw_on_rpc_error(
            amqp_login(
                impl_->connection,
                config.vhost,
                0,
                131072,
                30,
                AMQP_SASL_METHOD_PLAIN,
                config.username,
                config.password),
            "rabbitmq login");

        amqp_channel_open(impl_->connection, impl_->channel_id);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq channel open");

        declare_topology();
    } catch (const std::exception& ex) {
        impl_->last_error = ex.what();
        if (impl_->connection != nullptr) {
            amqp_destroy_connection(impl_->connection);
            impl_->connection = nullptr;
            impl_->socket = nullptr;
        }
    } catch (...) {
        impl_->last_error = "unknown rabbitmq initialization error";
        if (impl_->connection != nullptr) {
            amqp_destroy_connection(impl_->connection);
            impl_->connection = nullptr;
            impl_->socket = nullptr;
        }
    }
}

RabbitMqClient::~RabbitMqClient() {
    if (impl_ != nullptr && impl_->connection != nullptr) {
        amqp_channel_close(impl_->connection, impl_->channel_id, AMQP_REPLY_SUCCESS);
        amqp_connection_close(impl_->connection, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(impl_->connection);
    }
    delete impl_;
}

bool RabbitMqClient::available() const noexcept {
    return impl_ != nullptr && impl_->connection != nullptr;
}

std::string RabbitMqClient::last_error() const {
    if (impl_ == nullptr) {
        return "rabbitmq client implementation is missing";
    }
    return impl_->last_error;
}

void RabbitMqClient::reset_consumer() noexcept {
    if (impl_ != nullptr) {
        impl_->consumer_tag.clear();
    }
}

void RabbitMqClient::declare_topology() {
    if (!available()) {
        return;
    }

    auto retry_queue_arguments = make_retry_queue_arguments(impl_->config);
    try {
        amqp_exchange_declare(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_exchange),
            amqp_cstring_bytes("direct"),
            0,
            1,
            0,
            0,
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq declare exchange");

        amqp_queue_declare(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_queue),
            0,
            1,
            0,
            0,
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq declare queue");

        amqp_queue_declare(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_retry_queue),
            0,
            1,
            0,
            0,
            retry_queue_arguments);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq declare retry queue");

        amqp_queue_declare(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_dead_letter_queue),
            0,
            1,
            0,
            0,
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq declare dead-letter queue");

        amqp_queue_bind(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_queue),
            to_amqp_bytes(impl_->config.judge_exchange),
            to_amqp_bytes(impl_->config.judge_routing_key),
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq bind queue");

        amqp_queue_bind(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_retry_queue),
            to_amqp_bytes(impl_->config.judge_exchange),
            to_amqp_bytes(impl_->config.judge_retry_routing_key),
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq bind retry queue");

        amqp_queue_bind(
            impl_->connection,
            impl_->channel_id,
            to_amqp_bytes(impl_->config.judge_dead_letter_queue),
            to_amqp_bytes(impl_->config.judge_exchange),
            to_amqp_bytes(impl_->config.judge_dead_letter_routing_key),
            amqp_empty_table);
        throw_on_rpc_error(
            amqp_get_rpc_reply(impl_->connection),
            "rabbitmq bind dead-letter queue");
    } catch (...) {
        destroy_table_arguments(retry_queue_arguments);
        throw;
    }

    destroy_table_arguments(retry_queue_arguments);
}

bool RabbitMqClient::publish_judge_task(const std::string& body) {
    if (!available()) {
        return false;
    }

    return publish_message(
        impl_->connection,
        impl_->channel_id,
        impl_->config.judge_exchange,
        impl_->config.judge_routing_key,
        body);
}

bool RabbitMqClient::publish_retry_task(const std::string& body) {
    if (!available()) {
        return false;
    }

    return publish_message(
        impl_->connection,
        impl_->channel_id,
        impl_->config.judge_exchange,
        impl_->config.judge_retry_routing_key,
        body);
}

bool RabbitMqClient::publish_dead_letter_task(const std::string& body) {
    if (!available()) {
        return false;
    }

    return publish_message(
        impl_->connection,
        impl_->channel_id,
        impl_->config.judge_exchange,
        impl_->config.judge_dead_letter_routing_key,
        body);
}

std::optional<RabbitMqDelivery> RabbitMqClient::consume_one(int timeout_ms) {
    if (!available()) {
        return std::nullopt;
    }

    try {
        if (impl_->consumer_tag.empty()) {
            amqp_basic_qos(
                impl_->connection,
                impl_->channel_id,
                0,
                impl_->config.prefetch_count,
                0);
            throw_on_rpc_error(
                amqp_get_rpc_reply(impl_->connection),
                "rabbitmq basic qos");

            const auto reply = amqp_basic_consume(
                impl_->connection,
                impl_->channel_id,
                to_amqp_bytes(impl_->config.judge_queue),
                amqp_empty_bytes,
                0,
                0,
                0,
                amqp_empty_table);
            throw_on_rpc_error(
                amqp_get_rpc_reply(impl_->connection),
                "rabbitmq basic consume");

            impl_->consumer_tag = reply_text_to_string(reply->consumer_tag);
        }

        amqp_envelope_t envelope;
        ::timeval timeout{};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        amqp_maybe_release_buffers(impl_->connection);
        const auto reply = amqp_consume_message(
            impl_->connection,
            &envelope,
            &timeout,
            0);

        if (is_timeout_reply(reply)) {
            return std::nullopt;
        }

        throw_on_rpc_error(reply, "rabbitmq consume message");

        RabbitMqDelivery delivery;
        delivery.body = std::string{
            static_cast<const char*>(envelope.message.body.bytes),
            static_cast<std::size_t>(envelope.message.body.len)};
        delivery.delivery_tag = envelope.delivery_tag;
        amqp_destroy_envelope(&envelope);
        return delivery;
    } catch (...) {
        reset_consumer();
        return std::nullopt;
    }
}

void RabbitMqClient::ack(std::uint64_t delivery_tag) {
    if (available()) {
        amqp_basic_ack(impl_->connection, impl_->channel_id, delivery_tag, false);
    }
}

void RabbitMqClient::nack_requeue(std::uint64_t delivery_tag) {
    if (available()) {
        amqp_basic_nack(impl_->connection, impl_->channel_id, delivery_tag, false, true);
    }
}

void RabbitMqClient::reject_dead(std::uint64_t delivery_tag) {
    if (available()) {
        amqp_basic_reject(impl_->connection, impl_->channel_id, delivery_tag, false);
    }
}

} // namespace oj::common
