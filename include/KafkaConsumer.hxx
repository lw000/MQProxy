#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cppkafka/cppkafka.h>

namespace MQProxy {

class Config;

/**
 * @brief 重连所需的连接与安全配置快照（不依赖 Config 生命周期）。
 */
struct ConnectionConfig {
    std::string brokers;
    std::string groupId;
    std::string securityProtocol;
    std::string saslMechanism;
    std::string saslUsername;
    std::string saslPassword;
    std::string sslCaLocation;
    std::string sslKeyLocation;
    std::string sslCertificateLocation;
    std::string sslEndpointIdentificationAlgorithm;
};

/**
 * @brief Kafka 消费者：独立消费线程 + 可重连，所有权由本类管理。
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

    bool initialize(const Config& config);
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
    ConnectionConfig connection_;

    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    std::thread consumerThread_;

    MessageCallback messageCallback_;
};

} // namespace MQProxy
