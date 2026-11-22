#include "Config.hxx"

#include <toml.hpp>

#include <iostream>
#include <string>
#include <utility>

namespace MQProxy {
namespace {

/**
 * @brief 从 TOML 表读取一组 Kafka 配置；失败返回 false。
 *
 * 抽取自 DEV/PROD 两段重复逻辑，避免逐字重复（B1）。
 */
bool readKafkaConfig(const toml::table& data, const char* section, KafkaConfig& output) {
    const auto& node = data[section];
    if (!node.is_table()) {
        std::cerr << "Config: missing table [" << section << "]" << std::endl;
        return false;
    }
    output.brokers = node["brokers"].value<std::string>().value_or("");
    output.groupId = node["groupId"].value<std::string>().value_or("");
    output.saslUsername = node["saslUsername"].value<std::string>().value_or("");
    output.saslPassword = node["saslPassword"].value<std::string>().value_or("");
    output.sslCaLocation = node["sslCaLocation"].value<std::string>().value_or("");
    output.sslKeyLocation = node["sslKeyLocation"].value<std::string>().value_or("");
    output.sslCertificateLocation = node["sslCertificateLocation"].value<std::string>().value_or("");
    output.securityProtocol = node["securityProtocol"].value<std::string>().value_or("");
    output.saslMechanism = node["saslMechanism"].value<std::string>().value_or("");
    output.sslEndpointIdentificationAlgorithm =
        node["sslEndpointIdentificationAlgorithm"].value<std::string>().value_or("");

    output.topics.clear();
    const auto* topics = node["topics"].as_array();
    if (topics != nullptr) {
        for (const auto& topic : *topics) {
            const auto value = topic.value<std::string>();
            if (value.has_value() && !value->empty()) {
                output.topics.push_back(*value);
            }
        }
    }
    return true;
}

} // namespace

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& configFile) {
    try {
        const auto data = toml::parse_file(configFile);

        Config staging;
        staging.serviceName_ = data["serviceName"].value<std::string>().value_or("MQProxy");
        staging.mode_ = data["mode"].value<std::string>().value_or("consumer");
        staging.enableProd_ = data["enableProd"].value<bool>().value_or(false);
        staging.showContent_ = data["showContent"].value<bool>().value_or(true);
        staging.loggerConfig_.level = data["LOGGER"]["level"].value<int>().value_or(1);
        staging.loggerConfig_.outdir =
            data["LOGGER"]["outdir"].value<std::string>().value_or("logs");

        if (!readKafkaConfig(data, "DEV", staging.devConfig_)) {
            return false;
        }
        if (!readKafkaConfig(data, "PROD", staging.prodConfig_)) {
            return false;
        }
        if (staging.enableProd_ && staging.prodConfig_.topics.empty()) {
            std::cerr << "Config: PROD topics is empty" << std::endl;
            return false;
        }
        if (!staging.enableProd_ && staging.devConfig_.topics.empty()) {
            std::cerr << "Config: DEV topics is empty" << std::endl;
            return false;
        }

        serviceName_ = std::move(staging.serviceName_);
        mode_ = std::move(staging.mode_);
        enableProd_ = staging.enableProd_;
        showContent_ = staging.showContent_;
        loggerConfig_ = std::move(staging.loggerConfig_);
        devConfig_ = std::move(staging.devConfig_);
        prodConfig_ = std::move(staging.prodConfig_);
        return true;
    }
    catch (const std::exception& error) {
        std::cerr << "Config: failed to parse " << configFile << ": "
                  << error.what() << std::endl;
        return false;
    }
}

} // namespace MQProxy
