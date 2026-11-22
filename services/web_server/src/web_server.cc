#include "web_server/web_server.hxx"

#include "common/logger.hxx"
#include "config_store/config_store.hxx"
#include "kafka_manager/kafka_manager.hxx"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <utility>

namespace MQProxy {
namespace {

using json = nlohmann::json;

// 统一错误码：0=成功，1001=参数错误，1002=存储错误，1003=服务状态错误，9999=内部错误。
void sendResult(httplib::Response& res, int code, const std::string& message,
                const json& data = json::object()) {
    json body;
    body["code"] = code;
    body["message"] = message;
    body["data"] = data;
    res.status = 200;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool parseObject(const std::string& body, json& out) {
    out = json::parse(body, nullptr, false);
    return !out.is_discarded() && out.is_object();
}

json kafkaToJson(const KafkaRuntimeConfig& cfg) {
    return json{
        {"brokers", cfg.brokers},
        {"topics", cfg.topics},
        {"groupId", cfg.groupId},
        {"saslUsername", cfg.saslUsername},
        {"saslPassword", cfg.saslPassword},
        {"sslCaLocation", cfg.sslCaLocation},
        {"sslKeyLocation", cfg.sslKeyLocation},
        {"sslCertificateLocation", cfg.sslCertificateLocation},
        {"securityProtocol", cfg.securityProtocol},
        {"saslMechanism", cfg.saslMechanism},
        {"sslEndpointIdentificationAlgorithm", cfg.sslEndpointIdentificationAlgorithm},
    };
}

bool jsonToKafka(const json& obj, KafkaRuntimeConfig& cfg) {
    if (!obj.contains("brokers") || !obj["brokers"].is_string() ||
        !obj.contains("groupId") || !obj["groupId"].is_string() ||
        !obj.contains("topics") || !obj["topics"].is_array()) {
        return false;
    }
    cfg.brokers = obj["brokers"].get<std::string>();
    cfg.groupId = obj["groupId"].get<std::string>();
    cfg.topics.clear();
    for (const auto& topic : obj["topics"]) {
        if (!topic.is_string() || topic.get<std::string>().empty()) {
            return false;
        }
        cfg.topics.push_back(topic.get<std::string>());
    }
    if (cfg.brokers.empty() || cfg.groupId.empty() || cfg.topics.empty()) {
        return false;
    }

    const auto optionalString = [&obj, &cfg](const char* key, std::string& target) {
        if (obj.contains(key)) {
            if (!obj[key].is_string()) {
                return false;
            }
            target = obj[key].get<std::string>();
        }
        return true;
    };
    return optionalString("saslUsername", cfg.saslUsername) &&
           optionalString("saslPassword", cfg.saslPassword) &&
           optionalString("sslCaLocation", cfg.sslCaLocation) &&
           optionalString("sslKeyLocation", cfg.sslKeyLocation) &&
           optionalString("sslCertificateLocation", cfg.sslCertificateLocation) &&
           optionalString("securityProtocol", cfg.securityProtocol) &&
           optionalString("saslMechanism", cfg.saslMechanism) &&
           optionalString("sslEndpointIdentificationAlgorithm",
                          cfg.sslEndpointIdentificationAlgorithm);
}

json loggerToJson(const LoggerRuntimeConfig& cfg) {
    return json{{"level", cfg.level}, {"flushIntervalMs", cfg.flushIntervalMs}};
}

bool jsonToLogger(const json& obj, LoggerRuntimeConfig& cfg) {
    if (!obj.contains("level") || !obj["level"].is_number_integer() ||
        !obj.contains("flushIntervalMs") || !obj["flushIntervalMs"].is_number_integer()) {
        return false;
    }
    cfg.level = obj["level"].get<int>();
    cfg.flushIntervalMs = obj["flushIntervalMs"].get<int>();
    return cfg.level >= 0 && cfg.level <= 5 &&
           cfg.flushIntervalMs >= 0 && cfg.flushIntervalMs <= 600000;
}

} // namespace

WebServer::WebServer(ConfigStore& store, KafkaManager& kafka, Logger& logger)
    : store_(store), kafka_(kafka), logger_(logger) {}

WebServer::~WebServer() = default;

bool WebServer::on_init(const ServiceConfig& cfg) {
    server_ = std::make_unique<httplib::Server>();
    registerRoutes();
    if (!server_->set_mount_point("/", cfg.webRoot)) {
        LOG_ERROR_FMT("WebServer mount failed: {}", cfg.webRoot);
        return false;
    }
    LOG_INFO_FMT("WebServer prepared on {}:{}", cfg.httpHost, cfg.httpPort);
    return true;
}

bool WebServer::on_start() {
    const std::string host = config().httpHost;
    const int port = config().httpPort;
    listenThread_ = std::thread([this, host, port] {
        if (!server_->listen(host, port)) {
            LOG_ERROR_FMT("WebServer listen failed on {}:{}", host, port);
        }
    });
    return true;
}

void WebServer::on_stop() noexcept {
    if (server_) {
        server_->stop();
    }
    if (listenThread_.joinable()) {
        listenThread_.join();
    }
}

void WebServer::registerRoutes() {
    server_->Get("/api/status", [this](const httplib::Request&, httplib::Response& res) {
        KafkaManager::Status kafkaStatus;
        kafka_.status(kafkaStatus);
        json data;
        data["kafka"] = json{{"running", kafkaStatus.running},
                             {"brokers", kafkaStatus.brokers},
                             {"topics", kafkaStatus.topics},
                             {"groupId", kafkaStatus.groupId}};
        data["logger"] = json{{"level", logger_.level()},
                              {"flushIntervalMs", logger_.flushIntervalMs()}};
        data["web"] = json{{"host", config().httpHost}, {"port", config().httpPort}};
        sendResult(res, 0, "ok", data);
    });

    server_->Get("/api/kafka/config", [this](const httplib::Request&, httplib::Response& res) {
        KafkaRuntimeConfig cfg;
        if (!store_.getKafkaConfig(cfg)) {
            sendResult(res, 1002, "read kafka config failed");
            return;
        }
        sendResult(res, 0, "ok", kafkaToJson(cfg));
    });

    server_->Post("/api/kafka/config", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseObject(req.body, body)) {
            sendResult(res, 1001, "invalid JSON body");
            return;
        }
        KafkaRuntimeConfig cfg;
        if (!jsonToKafka(body, cfg)) {
            sendResult(res, 1001, "invalid kafka config");
            return;
        }
        if (!store_.setKafkaConfig(cfg)) {
            sendResult(res, 1002, "write kafka config failed");
            return;
        }
        sendResult(res, 0, "saved; click restart to apply", kafkaToJson(cfg));
    });

    server_->Post("/api/kafka/start", [this](const httplib::Request&, httplib::Response& res) {
        if (!kafka_.startOrResume()) {
            sendResult(res, 1003, "kafka start failed");
            return;
        }
        sendResult(res, 0, "kafka started");
    });

    server_->Post("/api/kafka/stop", [this](const httplib::Request&, httplib::Response& res) {
        kafka_.stop();
        sendResult(res, 0, "kafka stopped");
    });

    server_->Post("/api/kafka/restart", [this](const httplib::Request&, httplib::Response& res) {
        if (!kafka_.restart()) {
            sendResult(res, 1003, "kafka restart failed");
            return;
        }
        sendResult(res, 0, "kafka restarted with latest config");
    });

    server_->Get("/api/logger/config", [this](const httplib::Request&, httplib::Response& res) {
        LoggerRuntimeConfig cfg;
        if (!store_.getLoggerConfig(cfg)) {
            sendResult(res, 1002, "read logger config failed");
            return;
        }
        sendResult(res, 0, "ok", loggerToJson(cfg));
    });

    server_->Post("/api/logger/config", [this](const httplib::Request& req, httplib::Response& res) {
        json body;
        if (!parseObject(req.body, body)) {
            sendResult(res, 1001, "invalid JSON body");
            return;
        }
        LoggerRuntimeConfig cfg;
        if (!jsonToLogger(body, cfg)) {
            sendResult(res, 1001, "invalid logger config");
            return;
        }
        if (!store_.setLoggerConfig(cfg)) {
            sendResult(res, 1002, "write logger config failed");
            return;
        }
        sendResult(res, 0, "saved; click restart to apply", loggerToJson(cfg));
    });

    server_->Post("/api/logger/restart", [this](const httplib::Request&, httplib::Response& res) {
        LoggerRuntimeConfig cfg;
        if (!store_.getLoggerConfig(cfg)) {
            sendResult(res, 1002, "read logger config failed");
            return;
        }
        if (!logger_.setLevel(cfg.level) || !logger_.setFlushIntervalMs(cfg.flushIntervalMs)) {
            sendResult(res, 1001, "invalid stored logger config");
            return;
        }
        sendResult(res, 0, "logger runtime config applied", loggerToJson(cfg));
    });
}

} // namespace MQProxy
