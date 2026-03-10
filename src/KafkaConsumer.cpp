#include "KafkaConsumer.h"
#include "Config.h"
#include "Logger.h"
#include <librdkafka/rdkafkacpp.h>
#include <iostream>
#include <thread>
#include <chrono>

namespace MQProxy {

KafkaConsumer::KafkaConsumer()
    : running_(false)
    , initialized_(false) {
}

KafkaConsumer::~KafkaConsumer() {
    stop();
}

bool KafkaConsumer::initialize(const Config& config) {
    if (initialized_) {
        LOG_WARN("Kafka consumer already initialized");
        return true;
    }
    
    const auto& kafkaConfig = config.getKafkaConfig();
    brokers_ = kafkaConfig.brokers;
    groupId_ = kafkaConfig.groupId;
    
    try {
        // 创建配置对象
        config_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
        topicConfig_.reset(RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC));
        
        if (!config_ || !topicConfig_) {
            LOG_ERROR("Failed to create configuration");
            return false;
        }
        
        std::string errstr;
        
        // 设置基本配置
        if (config_->set("bootstrap.servers", brokers_, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set bootstrap.servers: {}", errstr);
            return false;
        }
        
        if (config_->set("group.id", groupId_, errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set group.id: {}", errstr);
            return false;
        }
        
        // 设置自动提交
        if (config_->set("enable.auto.commit", "true", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set enable.auto.commit: {}", errstr);
            return false;
        }
        
        if (config_->set("auto.commit.interval.ms", "5000", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set auto.commit.interval.ms: {}", errstr);
            return false;
        }
        
        // 设置从最新开始消费
        if (config_->set("auto.offset.reset", "latest", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set auto.offset.reset: {}", errstr);
            return false;
        }
        
        // 设置断线重连机制
        if (config_->set("reconnect.backoff.ms", "1000", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set reconnect.backoff.ms: {}", errstr);
            return false;
        }
        
        if (config_->set("reconnect.backoff.max.ms", "10000", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set reconnect.backoff.max.ms: {}", errstr);
            return false;
        }
        
        // 设置socket超时
        if (config_->set("socket.timeout.ms", "30000", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set socket.timeout.ms: {}", errstr);
            return false;
        }
        
        // 设置元数据刷新间隔
        if (config_->set("metadata.max.age.ms", "30000", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set metadata.max.age.ms: {}", errstr);
            return false;
        }
        
        // 禁止自动创建主题
        if (config_->set("allow.auto.create.topics", "false", errstr) != RdKafka::Conf::CONF_OK) {
            LOG_ERROR_FMT("Failed to set allow.auto.create.topics: {}", errstr);
            return false;
        }
        
        // 如果配置了SASL
        if (!kafkaConfig.securityProtocol.empty()) {
            if (config_->set("security.protocol", kafkaConfig.securityProtocol, errstr) != RdKafka::Conf::CONF_OK) {
                LOG_ERROR_FMT("Failed to set security.protocol: {}", errstr);
                return false;
            }
            
            if (!kafkaConfig.saslMechanism.empty()) {
                if (config_->set("sasl.mechanism", kafkaConfig.saslMechanism, errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR_FMT("Failed to set sasl.mechanism: {}", errstr);
                    return false;
                }
            }
            
            if (!kafkaConfig.saslUsername.empty()) {
                if (config_->set("sasl.username", kafkaConfig.saslUsername, errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR_FMT("Failed to set sasl.username: {}", errstr);
                    return false;
                }
            }
            
            if (!kafkaConfig.saslPassword.empty()) {
                if (config_->set("sasl.password", kafkaConfig.saslPassword, errstr) != RdKafka::Conf::CONF_OK) {
                    LOG_ERROR_FMT("Failed to set sasl.password: {}", errstr);
                    return false;
                }
            }
        }
        
        // 如果配置了SSL
        if (!kafkaConfig.sslCaLocation.empty()) {
            if (config_->set("ssl.ca.location", kafkaConfig.sslCaLocation, errstr) != RdKafka::Conf::CONF_OK) {
                LOG_ERROR_FMT("Failed to set ssl.ca.location: {}", errstr);
                return false;
            }
        }
        
        if (!kafkaConfig.sslKeyLocation.empty()) {
            if (config_->set("ssl.key.location", kafkaConfig.sslKeyLocation, errstr) != RdKafka::Conf::CONF_OK) {
                LOG_ERROR_FMT("Failed to set ssl.key.location: {}", errstr);
                return false;
            }
        }
        
        if (!kafkaConfig.sslCertificateLocation.empty()) {
            if (config_->set("ssl.certificate.location", kafkaConfig.sslCertificateLocation, errstr) != RdKafka::Conf::CONF_OK) {
                LOG_ERROR_FMT("Failed to set ssl.certificate.location: {}", errstr);
                return false;
            }
        }
        
        if (!kafkaConfig.sslEndpointIdentificationAlgorithm.empty()) {
            if (config_->set("ssl.endpoint.identification.algorithm", kafkaConfig.sslEndpointIdentificationAlgorithm, errstr) != RdKafka::Conf::CONF_OK) {
                LOG_ERROR_FMT("Failed to set ssl.endpoint.identification.algorithm: {}", errstr);
                return false;
            }
        }
        
        // 创建消费者
        consumer_.reset(RdKafka::KafkaConsumer::create(config_.get(), errstr));
        if (!consumer_) {
            LOG_ERROR_FMT("Failed to create Kafka consumer: {}", errstr);
            return false;
        }
        
        initialized_ = true;
        LOG_INFO_FMT("Kafka consumer initialized successfully, brokers: {}, groupId: {}", brokers_, groupId_);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Failed to initialize Kafka consumer: {}", e.what());
        return false;
    }
}

bool KafkaConsumer::subscribe(const std::vector<std::string>& topics) {
    if (!initialized_) {
        LOG_ERROR("Kafka consumer not initialized");
        return false;
    }
    
    try {
        std::string errstr;
        RdKafka::ErrorCode err = consumer_->subscribe(topics);
        if (err) {
            LOG_ERROR_FMT("Failed to subscribe to topics: {}", RdKafka::err2str(err));
            return false;
        }
        
        subscribedTopics_ = topics;
        LOG_INFO("Successfully subscribed to topics:");
        for (const auto& topic : topics) {
            LOG_INFO_FMT("  - {}", topic);
        }
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Failed to subscribe: {}", e.what());
        return false;
    }
}

void KafkaConsumer::start() {
    if (!initialized_) {
        LOG_ERROR("Kafka consumer not initialized");
        return;
    }
    
    if (running_) {
        LOG_WARN("Kafka consumer already running");
        return;
    }
    
    running_ = true;
    LOG_INFO("Starting Kafka consumer...");
    
    // 在新线程中启动消费循环
    std::thread consumerThread(&KafkaConsumer::consumeLoop, this);
    consumerThread.detach();
}

void KafkaConsumer::stop() {
    if (!running_) {
        return;
    }
    
    LOG_INFO("Stopping Kafka consumer...");
    running_ = false;
    
    if (consumer_) {
        try {
            consumer_->close();
            LOG_INFO("Kafka consumer closed");
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Error closing consumer: {}", e.what());
        }
    }
}

void KafkaConsumer::consumeLoop() {
    LOG_INFO("Consumer loop started");
    
    while (running_) {
        handleMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    LOG_INFO("Consumer loop stopped");
}

void KafkaConsumer::handleMessages() {
    if (!consumer_) {
        return;
    }
    
    const int timeoutMs = 1000;
    
    while (running_) {
        RdKafka::Message* msg = consumer_->consume(timeoutMs);
        
        if (!msg) {
            continue;
        }
        
        switch (msg->err()) {
            case RdKafka::ERR_NO_ERROR: {
                // 成功接收消息
                std::string topic = msg->topic_name();
                int partition = msg->partition();
                int64_t offset = msg->offset();
                
                std::string key;
                if (msg->key()) {
                    key = *msg->key();
                }
                
                std::string payload;
                if (msg->payload() != nullptr && msg->len() > 0) {
                    payload.assign(static_cast<const char*>(msg->payload()), msg->len());
                }
                
                if (messageCallback_) {
                    try {
                        messageCallback_(topic, partition, offset, key, payload);
                    } catch (const std::exception& e) {
                        LOG_ERROR_FMT("Error in message callback: {}", e.what());
                    }
                }
                
                break;
            }
            
            case RdKafka::ERR__TIMED_OUT:
                // 超时，继续循环
                break;
            
            case RdKafka::ERR__PARTITION_EOF: {
                // 到达分区末尾
                LOG_DEBUG_FMT("Reached end of {} partition [{}] offset {}",
                             msg->topic_name(),
                             msg->partition(),
                             msg->offset());
                break;
            }
            
            case RdKafka::ERR__UNKNOWN_TOPIC:
            case RdKafka::ERR__UNKNOWN_PARTITION: {
                LOG_ERROR_FMT("Consume failed: {} - {}, partition [{}]",
                            RdKafka::err2str(msg->err()),
                            msg->topic_name(),
                            msg->partition());
                break;
            }
            
            case RdKafka::ERR_LEADER_NOT_AVAILABLE:
            case RdKafka::ERR_NOT_LEADER_FOR_PARTITION:
            case RdKafka::ERR_BROKER_NOT_AVAILABLE:
            case RdKafka::ERR_REBALANCE_IN_PROGRESS: {
                // 临时性错误，稍后重试
                LOG_WARN_FMT("Temporary error, will retry: {}", RdKafka::err2str(msg->err()));
                std::this_thread::sleep_for(std::chrono::seconds(1));
                break;
            }
            
            case RdKafka::ERR__TRANSPORT: {
                // 网络传输错误，尝试重新连接
                LOG_ERROR_FMT("Transport error: {}, attempting to recover...", RdKafka::err2str(msg->err()));
                handleReconnect();
                break;
            }
            
            default:
                LOG_ERROR_FMT("Consume failed: {}", RdKafka::err2str(msg->err()));
                break;
        }
        
        delete msg;
    }
}

void KafkaConsumer::handleReconnect() {
    int retryCount = 0;
    const int maxRetries = 5;
    
    while (running_ && retryCount < maxRetries) {
        try {
            // 关闭当前消费者
            if (consumer_) {
                consumer_->close();
                consumer_.reset();
            }
            
            // 等待后重新创建消费者
            std::this_thread::sleep_for(std::chrono::seconds(2 + retryCount));
            
            // 重新创建消费者
            std::string errstr;
            consumer_.reset(RdKafka::KafkaConsumer::create(config_.get(), errstr));
            
            if (!consumer_) {
                LOG_ERROR_FMT("Failed to recreate Kafka consumer: {}", errstr);
                retryCount++;
                continue;
            }
            
            // 重新订阅主题
            RdKafka::ErrorCode err = consumer_->subscribe(subscribedTopics_);
            if (err) {
                LOG_ERROR_FMT("Failed to resubscribe to topics: {}", RdKafka::err2str(err));
                retryCount++;
                continue;
            }
            
            LOG_INFO("Successfully reconnected and resubscribed to topics");
            return;
            
        } catch (const std::exception& e) {
            LOG_ERROR_FMT("Reconnect failed: {}", e.what());
            retryCount++;
        }
    }
    
    LOG_ERROR("Failed to reconnect after max retries");
}

} // namespace MQProxy
