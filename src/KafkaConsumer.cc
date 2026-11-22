#include "KafkaConsumer.hxx"

#include "Config.hxx"
#include "Logger.hxx"

#include <chrono>
#include <exception>
#include <utility>

namespace MQProxy {
namespace {

constexpr int kPollTimeoutMs = 100;
constexpr int kMaxReconnectRetries = 5;

bool isTemporaryError(const std::string& errorText) {
    return errorText.find("TRANSPORT") != std::string::npos ||
           errorText.find("LEADER_NOT_AVAILABLE") != std::string::npos ||
           errorText.find("BROKER_NOT_AVAILABLE") != std::string::npos;
}

} // namespace

KafkaConsumer::~KafkaConsumer() {
    stop();
}

cppkafka::Configuration KafkaConsumer::buildConfiguration() const {
    cppkafka::Configuration configObj = {
        {"metadata.broker.list", connection_.brokers},
        {"group.id", connection_.groupId},
        {"enable.auto.commit", false},
        {"auto.commit.interval.ms", 5000},
        {"auto.offset.reset", "latest"},
        {"reconnect.backoff.ms", 1000},
        {"reconnect.backoff.max.ms", 10000},
        {"socket.timeout.ms", 30000},
        {"metadata.max.age.ms", 30000},
        {"allow.auto.create.topics", false},
    };

    if (!connection_.securityProtocol.empty()) {
        configObj.set("security.protocol", connection_.securityProtocol);
    }
    if (!connection_.saslMechanism.empty()) {
        configObj.set("sasl.mechanism", connection_.saslMechanism);
    }
    if (!connection_.saslUsername.empty()) {
        configObj.set("sasl.username", connection_.saslUsername);
    }
    if (!connection_.saslPassword.empty()) {
        configObj.set("sasl.password", connection_.saslPassword);
    }
    if (!connection_.sslCaLocation.empty()) {
        configObj.set("ssl.ca.location", connection_.sslCaLocation);
    }
    if (!connection_.sslKeyLocation.empty()) {
        configObj.set("ssl.key.location", connection_.sslKeyLocation);
    }
    if (!connection_.sslCertificateLocation.empty()) {
        configObj.set("ssl.certificate.location", connection_.sslCertificateLocation);
    }
    if (!connection_.sslEndpointIdentificationAlgorithm.empty()) {
        configObj.set("ssl.endpoint.identification.algorithm",
                      connection_.sslEndpointIdentificationAlgorithm);
    }
    return configObj;
}

bool KafkaConsumer::initialize(const Config& config) {
    if (initialized_.load()) {
        LOG_WARN("Kafka consumer already initialized");
        return true;
    }
    const auto& kafkaConfig = config.getKafkaConfig();
    connection_.brokers = kafkaConfig.brokers;
    connection_.groupId = kafkaConfig.groupId;
    connection_.securityProtocol = kafkaConfig.securityProtocol;
    connection_.saslMechanism = kafkaConfig.saslMechanism;
    connection_.saslUsername = kafkaConfig.saslUsername;
    connection_.saslPassword = kafkaConfig.saslPassword;
    connection_.sslCaLocation = kafkaConfig.sslCaLocation;
    connection_.sslKeyLocation = kafkaConfig.sslKeyLocation;
    connection_.sslCertificateLocation = kafkaConfig.sslCertificateLocation;
    connection_.sslEndpointIdentificationAlgorithm =
        kafkaConfig.sslEndpointIdentificationAlgorithm;

    try {
        std::lock_guard<std::mutex> lock(consumerMutex_);
        consumer_ = std::make_unique<cppkafka::Consumer>(buildConfiguration());
        consumer_->set_assignment_callback([](cppkafka::TopicPartitionList& partitions) {
            LOG_INFO_FMT("Assigned {} partitions", partitions.size());
        });
        consumer_->set_revocation_callback([](const cppkafka::TopicPartitionList& partitions) {
            LOG_INFO_FMT("Revoked {} partitions", partitions.size());
        });
        consumer_->set_rebalance_error_callback([](cppkafka::Error error) {
            LOG_ERROR_FMT("Rebalance error: {}", error.to_string());
        });
        initialized_.store(true);
        LOG_INFO_FMT("Kafka consumer initialized, brokers: {}, groupId: {}",
                     connection_.brokers, connection_.groupId);
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("Failed to initialize Kafka consumer: {}", error.what());
        return false;
    }
}

bool KafkaConsumer::subscribe(const std::vector<std::string>& topics) {
    if (!initialized_.load() || topics.empty()) {
        LOG_ERROR("Kafka consumer not initialized or topics empty");
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(consumerMutex_);
        if (!consumer_) {
            LOG_ERROR("Kafka consumer handle is null");
            return false;
        }
        consumer_->subscribe(topics);
        subscribedTopics_ = topics;
        LOG_INFO_FMT("Subscribed to {} topics", topics.size());
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("Failed to subscribe: {}", error.what());
        return false;
    }
}

void KafkaConsumer::start() {
    if (!initialized_.load()) {
        LOG_ERROR("Kafka consumer not initialized");
        return;
    }
    if (running_.exchange(true)) {
        LOG_WARN("Kafka consumer already running");
        return;
    }
    LOG_INFO("Starting Kafka consumer...");
    consumerThread_ = std::thread(&KafkaConsumer::consumeLoop, this);
}

void KafkaConsumer::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    LOG_INFO("Stopping Kafka consumer...");
    if (consumerThread_.joinable()) {
        consumerThread_.join();
    }
    try {
        std::lock_guard<std::mutex> lock(consumerMutex_);
        if (consumer_) {
            consumer_->unassign();
        }
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("Error closing consumer: {}", error.what());
    }
    LOG_INFO("Kafka consumer stopped");
}

bool KafkaConsumer::reconnectOnce(int retryIndex) {
    try {
        consumer_.reset();
        consumer_ = std::make_unique<cppkafka::Consumer>(buildConfiguration());
        if (!subscribedTopics_.empty()) {
            consumer_->subscribe(subscribedTopics_);
        }
        LOG_INFO_FMT("Reconnect attempt {} succeeded", retryIndex + 1);
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("Reconnect attempt {} failed: {}", retryIndex + 1, error.what());
        return false;
    }
}

void KafkaConsumer::consumeLoop() {
    LOG_INFO("Consumer loop started");
    int retryCount = 0;

    while (running_.load()) {
        try {
            cppkafka::Message message{nullptr};
            {
                std::lock_guard<std::mutex> lock(consumerMutex_);
                if (!consumer_ && retryCount < kMaxReconnectRetries) {
                    if (reconnectOnce(retryCount)) {
                        retryCount = 0;
                    }
                    else {
                        ++retryCount;
                    }
                }
                if (consumer_) {
                    message = consumer_->poll(std::chrono::milliseconds(kPollTimeoutMs));
                }
                else {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
            }
            if (!message) {
                continue;
            }
            if (message.get_error()) {
                if (message.is_eof()) {
                    continue;
                }
                const std::string errorText = message.get_error().to_string();
                if (isTemporaryError(errorText)) {
                    LOG_WARN_FMT("Temporary error: {}", errorText);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                else {
                    LOG_ERROR_FMT("Consume error: {}", errorText);
                }
                continue;
            }

            const std::string topic = message.get_topic();
            const int partition = message.get_partition();
            const int64_t offset = message.get_offset();
            const cppkafka::Buffer& keyBuffer = message.get_key();
            const cppkafka::Buffer& payloadBuffer = message.get_payload();
            std::string key;
            std::string payload;
            if (keyBuffer) {
                key.assign(reinterpret_cast<const char*>(keyBuffer.get_data()),
                           static_cast<std::size_t>(keyBuffer.get_size()));
            }
            if (payloadBuffer) {
                payload.assign(reinterpret_cast<const char*>(payloadBuffer.get_data()),
                               static_cast<std::size_t>(payloadBuffer.get_size()));
            }

            if (messageCallback_) {
                try {
                    messageCallback_(topic, partition, offset, key, payload);
                    std::lock_guard<std::mutex> lock(consumerMutex_);
                    if (consumer_) {
                        consumer_->commit(message);
                    }
                }
                catch (const std::exception& error) {
                    LOG_ERROR_FMT("Error in message callback: {}", error.what());
                }
            }
            retryCount = 0;
        }
        catch (const cppkafka::HandleException& error) {
            LOG_ERROR_FMT("Kafka handle exception: {}", error.what());
            std::lock_guard<std::mutex> lock(consumerMutex_);
            if (retryCount < kMaxReconnectRetries && reconnectOnce(retryCount)) {
                retryCount = 0;
            }
            else {
                ++retryCount;
            }
        }
        catch (const std::exception& error) {
            LOG_ERROR_FMT("Error in consume loop: {}", error.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    LOG_INFO("Consumer loop stopped");
}

} // namespace MQProxy
