#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "common/bootstrap_config.hxx"
#include "common/service_base.hxx"

namespace SQLite {
class Database;
}

namespace MQProxy {

/**
 * @brief SQLite 动态配置存储：每个模块一张表，首次启动用 TOML 种子初始化。
 *
 * 单连接 + 互斥保护，所有读写走参数化语句；私有实现不暴露 SQLiteCpp 类型。
 */
class ConfigStore : public ServiceBase {
public:
    explicit ConfigStore(const BootstrapConfig& bootstrap);
    ~ConfigStore() override;

    const char* name() const override { return "config_store"; }
    const std::string& id() const override { return id_; }

    bool getLoggerConfig(LoggerRuntimeConfig& out) const;
    bool setLoggerConfig(const LoggerRuntimeConfig& cfg);

    bool getKafkaConfig(KafkaRuntimeConfig& out) const;
    bool setKafkaConfig(const KafkaRuntimeConfig& cfg);

protected:
    bool on_init(const ServiceConfig& cfg) override;
    bool on_start() override { return true; }
    void on_stop() noexcept override;

private:
    void seedIfEmpty();

    BootstrapConfig bootstrap_;
    std::unique_ptr<SQLite::Database> db_;
    mutable std::mutex mutex_;
    std::string id_ = "mqproxy.config_store";
};

} // namespace MQProxy
