#pragma once

#include <memory>
#include <string>
#include <thread>

#include "common/service_base.hxx"

namespace httplib {
class Server;
}

namespace MQProxy {

class ConfigStore;
class KafkaManager;
class Logger;

/**
 * @brief Web 管理子服务：cpp-httplib 静态页面 + JSON REST API。
 */
class WebServer : public ServiceBase {
public:
    WebServer(ConfigStore& store, KafkaManager& kafka, Logger& logger);
    ~WebServer() override;

    const char* name() const override { return "web_server"; }
    const std::string& id() const override { return id_; }

protected:
    bool on_init(const ServiceConfig& cfg) override;
    bool on_start() override;
    void on_stop() noexcept override;

private:
    void registerRoutes();

    ConfigStore& store_;
    KafkaManager& kafka_;
    Logger& logger_;
    std::unique_ptr<httplib::Server> server_;
    std::thread listenThread_;
    std::string id_ = "mqproxy.web_server";
};

} // namespace MQProxy
