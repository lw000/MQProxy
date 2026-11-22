#pragma once

#include <string>
#include <vector>

#include "common/service_base.hxx"

namespace MQProxy {

/** @brief logger 模块动态配置（存 SQLite logger_config 表）。 */
struct LoggerRuntimeConfig {
    int level = 1;                  ///< 0=trace..5=critical
    int flushIntervalMs = 1000;     ///< 定时刷新毫秒，0=关闭
};

/** @brief kafka 模块动态配置（存 SQLite kafka_config 表）。 */
struct KafkaRuntimeConfig {
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
 * @brief 启动依赖配置：从独立 TOML 文件读取，load() 成功前 getter 返回安全默认值。
 *
 * DEV/PROD 与 logger 段仅作为 SQLite 首次启动的种子，运行期以库内配置为准。
 */
class BootstrapConfig {
public:
    bool load(const std::string& configFile);

    const std::string& getServiceName() const { return serviceName_; }
    const std::string& getMode() const { return mode_; }
    bool isEnableProd() const { return enableProd_; }
    bool isShowContent() const { return showContent_; }

    LoggerRuntimeConfig getLoggerSeed() const { return loggerSeed_; }
    KafkaRuntimeConfig getKafkaSeed() const { return enableProd_ ? prodSeed_ : devSeed_; }

    /** @brief 转换为服务生命周期所需的启动依赖配置。 */
    ServiceConfig toServiceConfig() const;

private:
    std::string serviceName_ = "MQProxy";
    std::string mode_ = "consumer";
    bool enableProd_ = false;
    bool showContent_ = true;
    LoggerRuntimeConfig loggerSeed_;
    KafkaRuntimeConfig devSeed_;
    KafkaRuntimeConfig prodSeed_;

    std::string logOutdir_ = "logs";
    std::string dbPath_ = "mqproxy.db";
    std::string httpHost_ = "127.0.0.1";
    int httpPort_ = 8080;
    std::string webRoot_ = "web";
};

} // namespace MQProxy
