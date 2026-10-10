#pragma once

#include <string>
#include <vector>
#include <memory>

namespace MQProxy {

	struct LoggerConfig {
		int level;
		std::string outdir;
	};

	struct KafkaConfig {
		std::string brokers;
		std::vector<std::string> topics;
		std::string groupId;
		std::string saslUsername;
		std::string saslPassword;
		std::string sslCaLocation;
		std::string sslKeyLocation;
		std::string sslCertificateLocation;
		std::string securityProtocol;
		std::string saslMechanism;
		std::string sslEndpointIdentificationAlgorithm;
	};

	class Config {
	public:
		static Config& getInstance();

		bool load(const std::string& configFile);

		const std::string& getServiceName() const { return serviceName_; }
		const std::string& getMode() const { return mode_; }
		bool isEnableProd() const { return enableProd_; }
		bool isShowContent() const { return showContent_; }

		const LoggerConfig& getLoggerConfig() const { return loggerConfig_; }
		const KafkaConfig& getKafkaConfig() const;

	private:
		Config() = default;
		~Config() = default;
		Config(const Config&) = delete;
		Config& operator=(const Config&) = delete;

		std::string serviceName_;
		std::string mode_;
		bool enableProd_;
		bool showContent_;
		LoggerConfig loggerConfig_;
		KafkaConfig devConfig_;
		KafkaConfig prodConfig_;
	};

} // namespace MQProxy
