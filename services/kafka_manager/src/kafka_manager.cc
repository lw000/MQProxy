#include "kafka_manager/kafka_manager.hxx"

#include "common/logger.hxx"
#include "config_store/config_store.hxx"
#include "kafka_consumer.hxx"

#include <utility>

namespace MQProxy {

KafkaManager::KafkaManager(ConfigStore& store) : store_(store) {}

KafkaManager::~KafkaManager() = default;

void KafkaManager::installDefaultCallback() {
    messageCallback_ = [this](const std::string& topic, int partition, int64_t offset,
                              const std::string& key, const std::string& payload) {
        LOG_INFO_FMT("Received message - Topic: {}, Partition: {}, Offset: {}",
                     topic, partition, offset);
        if (!key.empty()) {
            LOG_DEBUG_FMT("Key: {}", key);
        }
        if (showContent_) {
            LOG_DEBUG_FMT("Payload: {}", payload);
        }
        else {
            LOG_DEBUG_FMT("Payload length: {} bytes", payload.size());
        }
    };
}

void KafkaManager::setMessageCallback(MessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    messageCallback_ = std::move(callback);
    if (consumer_) {
        consumer_->setMessageCallback(messageCallback_);
    }
}

bool KafkaManager::loadConfig() {
    if (!store_.getKafkaConfig(config_)) {
        LOG_ERROR("KafkaManager failed to load config from SQLite");
        return false;
    }
    return true;
}

bool KafkaManager::on_init(const ServiceConfig&) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loadConfig()) {
        return false;
    }
    consumer_ = std::make_unique<KafkaConsumer>();
    if (!messageCallback_) {
        installDefaultCallback();
    }
    consumer_->setMessageCallback(messageCallback_);
    if (!consumer_->initialize(config_)) {
        return false;
    }
    LOG_INFO_FMT("KafkaManager loaded config: brokers={}, topics={}, groupId={}",
                 config_.brokers, config_.topics.size(), config_.groupId);
    return true;
}

bool KafkaManager::on_start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!consumer_ || !consumer_->subscribe(config_.topics)) {
        return false;
    }
    consumer_->start();
    return true;
}

void KafkaManager::on_stop() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (consumer_) {
        consumer_->stop();
    }
}

bool KafkaManager::startOrResume() {
    if (state() == ServiceState::kStopped) {
        return restart();
    }
    return start();
}

bool KafkaManager::status(Status& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    out.running = consumer_ != nullptr && consumer_->isRunning();
    out.brokers = config_.brokers;
    out.topics = config_.topics;
    out.groupId = config_.groupId;
    return true;
}

} // namespace MQProxy
