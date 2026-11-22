#include "common/bootstrap_config.hxx"

#include <toml.hpp>

#include <iostream>
#include <utility>

namespace MQProxy {
namespace {

bool readKafkaConfig(const toml::table& data, const char* section, KafkaRuntimeConfig& output) {
    const auto& node = data[section];
    if (!node.is_table()) {
        std::cerr << "BootstrapConfig: missing table [" << section << "]" << std::endl;
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
    if (const auto* topics = node["topics"].as_array()) {
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

bool BootstrapConfig::load(const std::string& configFile) {
    try {
        const auto data = toml::parse_file(configFile);

        BootstrapConfig staging;
        staging.serviceName_ = data["serviceName"].value<std::string>().value_or("MQProxy");
        staging.mode_ = data["mode"].value<std::string>().value_or("consumer");
        staging.enableProd_ = data["enableProd"].value<bool>().value_or(false);
        staging.showContent_ = data["showContent"].value<bool>().value_or(true);

        staging.loggerSeed_.level = data["LOGGER"]["level"].value<int>().value_or(1);
        staging.loggerSeed_.flushIntervalMs =
            data["LOGGER"]["flushIntervalMs"].value<int>().value_or(1000);
        staging.logOutdir_ = data["LOGGER"]["outdir"].value<std::string>().value_or("logs");

        staging.dbPath_ = data["DATABASE"]["path"].value<std::string>().value_or("mqproxy.db");
        staging.httpHost_ = data["SERVER"]["host"].value<std::string>().value_or("127.0.0.1");
        staging.httpPort_ = data["SERVER"]["port"].value<int>().value_or(8080);
        staging.webRoot_ = data["SERVER"]["webroot"].value<std::string>().value_or("web");

        if (!readKafkaConfig(data, "DEV", staging.devSeed_)) {
            return false;
        }
        if (!readKafkaConfig(data, "PROD", staging.prodSeed_)) {
            return false;
        }
        if (staging.enableProd_ && staging.prodSeed_.topics.empty()) {
            std::cerr << "BootstrapConfig: PROD topics is empty" << std::endl;
            return false;
        }
        if (!staging.enableProd_ && staging.devSeed_.topics.empty()) {
            std::cerr << "BootstrapConfig: DEV topics is empty" << std::endl;
            return false;
        }

        *this = std::move(staging);
        return true;
    }
    catch (const std::exception& error) {
        std::cerr << "BootstrapConfig: failed to parse " << configFile << ": "
                  << error.what() << std::endl;
        return false;
    }
}

ServiceConfig BootstrapConfig::toServiceConfig() const {
    ServiceConfig cfg;
    cfg.dbPath = dbPath_;
    cfg.httpHost = httpHost_;
    cfg.httpPort = httpPort_;
    cfg.webRoot = webRoot_;
    cfg.logOutdir = logOutdir_;
    cfg.logLevel = loggerSeed_.level;
    cfg.logFlushIntervalMs = loggerSeed_.flushIntervalMs;
    return cfg;
}

} // namespace MQProxy
