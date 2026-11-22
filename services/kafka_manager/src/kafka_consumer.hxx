#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <cppkafka/cppkafka.h>

#include "common/bootstrap_config.hxx"

namespace MQProxy {

/**
 * @brief Kafka 消费者：独立消费线程 + 可重连；私有实现，仅由 KafkaManager 使用。
 */
class KafkaConsumer {
public:
    using MessageCallback = std::function<void(const std::string& topic,
                                               int partition,
                                               int64_t offset,
                                               const std::string& key,
                                               const std::string& payload)>;

    KafkaConsumer() = default;
    ~KafkaConsumer();

    KafkaConsumer(const KafkaConsumer&) = delete;
    KafkaConsumer& operator=(const KafkaConsumer&) = delete;

    bool initialize(const KafkaRuntimeConfig& cfg);
    bool subscribe(const std::vector<std::string>& topics);
    void start();
    void stop();
    bool isRunning() const { return running_.load(); }

    void setMessageCallback(MessageCallback callback) {
        messageCallback_ = std::move(callback);
    }

private:
    cppkafka::Configuration buildConfiguration() const;
    void consumeLoop();
    bool reconnectOnce(int retryIndex);

    std::unique_ptr<cppkafka::Consumer> consumer_;
    mutable std::mutex consumerMutex_;
    std::vector<std::string> subscribedTopics_;
    KafkaRuntimeConfig config_;

    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread consumerThread_;

    MessageCallback messageCallback_;
};

} // namespace MQProxy
