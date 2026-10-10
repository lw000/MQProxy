#include "Config.hxx"
#include <toml.hpp>
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace MQProxy {

	Config& Config::getInstance() {
		static Config instance;
		return instance;
	}

	bool Config::load(const std::string& configFile) {
		try {
			auto data = toml::parse_file(configFile);

			// 读取基础配置
			serviceName_ = data["serviceName"].value<std::string>().value_or("MQProxy");
			mode_ = data["mode"].value<std::string>().value_or("consumer");
			enableProd_ = data["enableProd"].value<bool>().value_or(false);
			showContent_ = data["showContent"].value<bool>().value_or(true);

			// 读取日志配置
			loggerConfig_.level = data["LOGGER"]["level"].value<int>().value_or(1);
			loggerConfig_.outdir = data["LOGGER"]["outdir"].value<std::string>().value_or("logs");

			// 读取DEV配置
			devConfig_.brokers = data["DEV"]["brokers"].value<std::string>().value_or("");
			devConfig_.groupId = data["DEV"]["groupId"].value<std::string>().value_or("");
			devConfig_.saslUsername = data["DEV"]["saslUsername"].value<std::string>().value_or("");
			devConfig_.saslPassword = data["DEV"]["saslPassword"].value<std::string>().value_or("");
			devConfig_.sslCaLocation = data["DEV"]["sslCaLocation"].value<std::string>().value_or("");
			devConfig_.sslKeyLocation = data["DEV"]["sslKeyLocation"].value<std::string>().value_or("");
			devConfig_.sslCertificateLocation = data["DEV"]["sslCertificateLocation"].value<std::string>().value_or("");
			devConfig_.securityProtocol = data["DEV"]["securityProtocol"].value<std::string>().value_or("");
			devConfig_.saslMechanism = data["DEV"]["saslMechanism"].value<std::string>().value_or("");
			devConfig_.sslEndpointIdentificationAlgorithm = data["DEV"]["sslEndpointIdentificationAlgorithm"].value<std::string>().value_or("");

			// 读取DEV topics
			auto* devTopicsArr = data["DEV"]["topics"].as_array();
			if (devTopicsArr) {
				for (const auto& topic : *devTopicsArr) {
					devConfig_.topics.push_back(topic.value<std::string>().value_or(""));
				}
			}

			// 读取PROD配置
			prodConfig_.brokers = data["PROD"]["brokers"].value<std::string>().value_or("");
			prodConfig_.groupId = data["PROD"]["groupId"].value<std::string>().value_or("");
			prodConfig_.saslUsername = data["PROD"]["saslUsername"].value<std::string>().value_or("");
			prodConfig_.saslPassword = data["PROD"]["saslPassword"].value<std::string>().value_or("");
			prodConfig_.sslCaLocation = data["PROD"]["sslCaLocation"].value<std::string>().value_or("");
			prodConfig_.sslKeyLocation = data["PROD"]["sslKeyLocation"].value<std::string>().value_or("");
			prodConfig_.sslCertificateLocation = data["PROD"]["sslCertificateLocation"].value<std::string>().value_or("");
			prodConfig_.securityProtocol = data["PROD"]["securityProtocol"].value<std::string>().value_or("");
			prodConfig_.saslMechanism = data["PROD"]["saslMechanism"].value<std::string>().value_or("");
			prodConfig_.sslEndpointIdentificationAlgorithm = data["PROD"]["sslEndpointIdentificationAlgorithm"].value<std::string>().value_or("");

			// 读取PROD topics
			auto* prodTopicsArr = data["PROD"]["topics"].as_array();
			if (prodTopicsArr) {
				for (const auto& topic : *prodTopicsArr) {
					prodConfig_.topics.push_back(topic.value<std::string>().value_or(""));
				}
			}

			return true;
		}
		catch (const std::exception& e) {
			std::cerr << "Failed to parse config file: " << e.what() << std::endl;
			return false;
		}
	}

	const KafkaConfig& Config::getKafkaConfig() const {
		return enableProd_ ? prodConfig_ : devConfig_;
	}

} // namespace MQProxy
