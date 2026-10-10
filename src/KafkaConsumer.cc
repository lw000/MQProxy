#include "KafkaConsumer.hxx"
#include "Config.hxx"
#include "Logger.hxx"
#include <cppkafka/cppkafka.h>
#include <iostream>

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
			// 创建配置
			cppkafka::Configuration configObj = {
				{ "metadata.broker.list", brokers_ },
				{ "group.id", groupId_ },
				{ "enable.auto.commit", true },
				{ "auto.commit.interval.ms", 5000 },
				{ "auto.offset.reset", "latest" },
				{ "reconnect.backoff.ms", 1000 },
				{ "reconnect.backoff.max.ms", 10000 },
				{ "socket.timeout.ms", 30000 },
				{ "metadata.max.age.ms", 30000 },
				{ "allow.auto.create.topics", false }
			};

			// 如果配置了SASL
			if (!kafkaConfig.securityProtocol.empty()) {
				configObj.set("security.protocol", kafkaConfig.securityProtocol);

				if (!kafkaConfig.saslMechanism.empty()) {
					configObj.set("sasl.mechanism", kafkaConfig.saslMechanism);
				}

				if (!kafkaConfig.saslUsername.empty()) {
					configObj.set("sasl.username", kafkaConfig.saslUsername);
				}

				if (!kafkaConfig.saslPassword.empty()) {
					configObj.set("sasl.password", kafkaConfig.saslPassword);
				}
			}

			// 如果配置了SSL
			if (!kafkaConfig.sslCaLocation.empty()) {
				configObj.set("ssl.ca.location", kafkaConfig.sslCaLocation);
			}

			if (!kafkaConfig.sslKeyLocation.empty()) {
				configObj.set("ssl.key.location", kafkaConfig.sslKeyLocation);
			}

			if (!kafkaConfig.sslCertificateLocation.empty()) {
				configObj.set("ssl.certificate.location", kafkaConfig.sslCertificateLocation);
			}

			if (!kafkaConfig.sslEndpointIdentificationAlgorithm.empty()) {
				configObj.set("ssl.endpoint.identification.algorithm", kafkaConfig.sslEndpointIdentificationAlgorithm);
			}

			// 创建消费者
			consumer_ = std::make_unique<cppkafka::Consumer>(configObj);

			// 设置rebalance回调
			consumer_->set_assignment_callback([](cppkafka::TopicPartitionList& topic_partitions) {
				LOG_INFO_FMT("Assigned {} partitions", topic_partitions.size());
				});

			consumer_->set_revocation_callback([](const cppkafka::TopicPartitionList& topic_partitions) {
				LOG_INFO_FMT("Revoked {} partitions", topic_partitions.size());
				});

			consumer_->set_rebalance_error_callback([](cppkafka::Error error) {
				LOG_ERROR_FMT("Rebalance error: {}", error.to_string());
				});

			initialized_ = true;
			LOG_INFO_FMT("Kafka consumer initialized successfully, brokers: {}, groupId: {}", brokers_, groupId_);
			return true;

		}
		catch (const std::exception& e) {
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
			consumer_->subscribe(topics);
			subscribedTopics_ = topics;

			LOG_INFO("Successfully subscribed to topics:");
			for (const auto& topic : topics) {
				LOG_INFO_FMT("  - {}", topic);
			}
			return true;

		}
		catch (const std::exception& e) {
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
				consumer_->unassign();
				LOG_INFO("Kafka consumer closed");
			}
			catch (const std::exception& e) {
				LOG_ERROR_FMT("Error closing consumer: {}", e.what());
			}
		}
	}

	void KafkaConsumer::consumeLoop() {
		LOG_INFO("Consumer loop started");

		while (running_) {
			try {
				// 消费消息，超时时间100ms
				cppkafka::Message msg = consumer_->poll(std::chrono::milliseconds(100));

				if (!msg) {
					continue;
				}

				// 检查是否有错误
				if (msg.get_error()) {
					if (msg.is_eof()) {
						// 分区到达末尾
						continue;
					}

					// 检查是否是临时性错误
					std::string error_str = msg.get_error().to_string();
					if (error_str.find("TRANSPORT") != std::string::npos ||
						error_str.find("LEADER_NOT_AVAILABLE") != std::string::npos) {
						LOG_WARN_FMT("Temporary error, will retry: {}", error_str);
						std::this_thread::sleep_for(std::chrono::seconds(1));
						continue;
					}

					LOG_ERROR_FMT("Consume error: {}", error_str);
					continue;
				}

				// 成功接收消息
				std::string topic = msg.get_topic();
				int partition = msg.get_partition();
				int64_t offset = msg.get_offset();

				std::string key;
				if (msg.get_key()) {
					key = msg.get_key();
				}

				std::string payload;
				if (msg.get_payload()) {
					payload = msg.get_payload();
				}

				if (messageCallback_) {
					try {
						messageCallback_(topic, partition, offset, key, payload);
					}
					catch (const std::exception& e) {
						LOG_ERROR_FMT("Error in message callback: {}", e.what());
					}
				}

			}
			catch (const cppkafka::HandleException& e) {
				LOG_ERROR_FMT("Kafka handle exception: {}", e.what());
				handleReconnect();
			}
			catch (const std::exception& e) {
				LOG_ERROR_FMT("Error in consume loop: {}", e.what());
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		LOG_INFO("Consumer loop stopped");
	}

	void KafkaConsumer::handleReconnect() {
		int retryCount = 0;
		const int maxRetries = 5;

		while (running_ && retryCount < maxRetries) {
			try {
				// 关闭当前消费者
				if (consumer_) {
					consumer_->unassign();
					consumer_.reset();
				}

				// 等待后重新创建消费者
				std::this_thread::sleep_for(std::chrono::seconds(2 + retryCount));

				// 重新创建消费者
				cppkafka::Configuration configObj = {
					{ "metadata.broker.list", brokers_ },
					{ "group.id", groupId_ },
					{ "enable.auto.commit", true },
					{ "auto.commit.interval.ms", 5000 },
					{ "auto.offset.reset", "latest" },
					{ "reconnect.backoff.ms", 1000 },
					{ "reconnect.backoff.max.ms", 10000 },
					{ "socket.timeout.ms", 30000 },
					{ "metadata.max.age.ms", 30000 },
					{ "allow.auto.create.topics", false }
				};

				consumer_ = std::make_unique<cppkafka::Consumer>(configObj);

				// 重新订阅主题
				consumer_->subscribe(subscribedTopics_);

				LOG_INFO("Successfully reconnected and resubscribed to topics");
				return;

			}
			catch (const std::exception& e) {
				LOG_ERROR_FMT("Reconnect failed: {}", e.what());
				retryCount++;
			}
		}

		LOG_ERROR("Failed to reconnect after max retries");
	}

} // namespace MQProxy
