#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <cppkafka/cppkafka.h>

namespace MQProxy {

class Config;

class KafkaConsumer {
public:
    using MessageCallback = std::function<void(const std::string& topic,
                                                int partition,
                                                int64_t offset,
                                                const std::string& key,
                                                const std::string& payload)>;

    KafkaConsumer();
    ~KafkaConsumer();

    bool initialize(const Config& config);
    bool subscribe(const std::vector<std::string>& topics);
    void start();
    void stop();
    bool isRunning() const { return running_; }

    void setMessageCallback(MessageCallback callback) {
        messageCallback_ = std::move(callback);
    }

private:
    void consumeLoop();
    void handleReconnect();

    std::unique_ptr<cppkafka::Consumer> consumer_;
    std::vector<std::string> subscribedTopics_;

    std::string brokers_;
    std::string groupId_;

    std::atomic<bool> running_;
    std::atomic<bool> initialized_;

    MessageCallback messageCallback_;
};

} // namespace MQProxy
