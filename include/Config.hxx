#pragma once

#include <string>
#include <vector>

namespace MQProxy {

/**
 * @brief 日志配置。
 */
struct LoggerConfig {
    int level = 1;             ///< 日志级别 0=trace..5=critical
    std::string outdir = "logs"; ///< 日志输出目录
};

/**
 * @brief Kafka 消费者配置（DEV/PROD 共用结构）。
 */
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

/**
 * @brief 应用配置单例；load() 成功前所有 getter 返回安全默认值。
 */
class Config {
public:
    static Config& getInstance();

    /**
     * @brief 从 TOML 文件加载配置，可重复调用且幂等。
     * @return 成功 true；解析失败保留原有配置并返回 false。
     */
    bool load(const std::string& configFile);

    const std::string& getServiceName() const { return serviceName_; }
    const std::string& getMode() const { return mode_; }
    bool isEnableProd() const { return enableProd_; }
    bool isShowContent() const { return showContent_; }

    const LoggerConfig& getLoggerConfig() const { return loggerConfig_; }
    const KafkaConfig& getKafkaConfig() const { return enableProd_ ? prodConfig_ : devConfig_; }

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::string serviceName_ = "MQProxy";
    std::string mode_ = "consumer";
    bool enableProd_ = false;
    bool showContent_ = true;
    LoggerConfig loggerConfig_;
    KafkaConfig devConfig_;
    KafkaConfig prodConfig_;
};

} // namespace MQProxy
