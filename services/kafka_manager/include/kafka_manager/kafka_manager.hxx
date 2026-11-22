#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/bootstrap_config.hxx"
#include "common/service_base.hxx"

namespace MQProxy {

class ConfigStore;
class KafkaConsumer;

/**
 * @brief Kafka 子服务：从 SQLite 读取动态配置，提供启动/停止/重启与状态查询。
 */
class KafkaManager : public ServiceBase {
public:
    explicit KafkaManager(ConfigStore& store);
    ~KafkaManager() override;

    const char* name() const override { return "kafka_manager"; }
    const std::string& id() const override { return id_; }

    struct Status {
        bool running = false;
        std::string brokers;
        std::vector<std::string> topics;
        std::string groupId;
    };

    bool status(Status& out) const;

    /** @brief 停止后再次启动走 restart()，保证配置与消费者被重建。 */
    bool startOrResume();

    void setShowContent(bool show) { showContent_ = show; }

    using MessageCallback = std::function<void(const std::string& topic,
                                               int partition,
                                               int64_t offset,
                                               const std::string& key,
                                               const std::string& payload)>;
    void setMessageCallback(MessageCallback callback);

protected:
    bool on_init(const ServiceConfig& cfg) override;
    bool on_start() override;
    void on_stop() noexcept override;

private:
    bool loadConfig();
    void installDefaultCallback();

    ConfigStore& store_;
    std::unique_ptr<KafkaConsumer> consumer_;
    KafkaRuntimeConfig config_;
    MessageCallback messageCallback_;
    mutable std::mutex mutex_;
    bool showContent_ = true;
    std::string id_ = "mqproxy.kafka_manager";
};

} // namespace MQProxy
