#include "config_store/config_store.hxx"

#include "common/logger.hxx"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <exception>

namespace MQProxy {

ConfigStore::ConfigStore(const BootstrapConfig& bootstrap) : bootstrap_(bootstrap) {}

ConfigStore::~ConfigStore() = default;

bool ConfigStore::on_init(const ServiceConfig& cfg) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        db_ = std::make_unique<SQLite::Database>(
            cfg.dbPath, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 3000);

        db_->exec("CREATE TABLE IF NOT EXISTS logger_config ("
                  "id INTEGER PRIMARY KEY CHECK (id = 1),"
                  "level INTEGER NOT NULL,"
                  "flush_interval_ms INTEGER NOT NULL,"
                  "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
        db_->exec("CREATE TABLE IF NOT EXISTS kafka_config ("
                  "id INTEGER PRIMARY KEY CHECK (id = 1),"
                  "brokers TEXT NOT NULL,"
                  "topics TEXT NOT NULL,"
                  "group_id TEXT NOT NULL,"
                  "sasl_username TEXT NOT NULL,"
                  "sasl_password TEXT NOT NULL,"
                  "ssl_ca_location TEXT NOT NULL,"
                  "ssl_key_location TEXT NOT NULL,"
                  "ssl_certificate_location TEXT NOT NULL,"
                  "security_protocol TEXT NOT NULL,"
                  "sasl_mechanism TEXT NOT NULL,"
                  "ssl_endpoint_identification_algorithm TEXT NOT NULL,"
                  "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
        seedIfEmpty();
        LOG_INFO_FMT("ConfigStore opened {}", cfg.dbPath);
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore init failed: {}", error.what());
        return false;
    }
}

void ConfigStore::on_stop() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    db_.reset();
}

void ConfigStore::seedIfEmpty() {
    const LoggerRuntimeConfig loggerSeed = bootstrap_.getLoggerSeed();
    SQLite::Statement loggerCount(*db_, "SELECT COUNT(*) FROM logger_config");
    if (loggerCount.executeStep() && loggerCount.getColumn(0).getInt() == 0) {
        SQLite::Statement insert(*db_,
            "INSERT INTO logger_config(id, level, flush_interval_ms) VALUES(1, ?, ?)");
        insert.bind(1, loggerSeed.level);
        insert.bind(2, loggerSeed.flushIntervalMs);
        insert.exec();
    }

    SQLite::Statement kafkaCount(*db_, "SELECT COUNT(*) FROM kafka_config");
    if (kafkaCount.executeStep() && kafkaCount.getColumn(0).getInt() == 0) {
        const KafkaRuntimeConfig kafkaSeed = bootstrap_.getKafkaSeed();
        const nlohmann::json topics = kafkaSeed.topics;
        SQLite::Statement insert(*db_,
            "INSERT INTO kafka_config(id, brokers, topics, group_id, sasl_username, sasl_password, "
            "ssl_ca_location, ssl_key_location, ssl_certificate_location, security_protocol, "
            "sasl_mechanism, ssl_endpoint_identification_algorithm) "
            "VALUES(1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        insert.bind(1, kafkaSeed.brokers);
        insert.bind(2, topics.dump());
        insert.bind(3, kafkaSeed.groupId);
        insert.bind(4, kafkaSeed.saslUsername);
        insert.bind(5, kafkaSeed.saslPassword);
        insert.bind(6, kafkaSeed.sslCaLocation);
        insert.bind(7, kafkaSeed.sslKeyLocation);
        insert.bind(8, kafkaSeed.sslCertificateLocation);
        insert.bind(9, kafkaSeed.securityProtocol);
        insert.bind(10, kafkaSeed.saslMechanism);
        insert.bind(11, kafkaSeed.sslEndpointIdentificationAlgorithm);
        insert.exec();
    }
}

bool ConfigStore::getLoggerConfig(LoggerRuntimeConfig& out) const {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) {
            return false;
        }
        SQLite::Statement query(*db_,
            "SELECT level, flush_interval_ms FROM logger_config WHERE id = 1");
        if (!query.executeStep()) {
            return false;
        }
        out.level = query.getColumn(0).getInt();
        out.flushIntervalMs = query.getColumn(1).getInt();
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore getLoggerConfig failed: {}", error.what());
        return false;
    }
}

bool ConfigStore::setLoggerConfig(const LoggerRuntimeConfig& cfg) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) {
            return false;
        }
        SQLite::Statement update(*db_,
            "UPDATE logger_config SET level = ?, flush_interval_ms = ?, "
            "updated_at = CURRENT_TIMESTAMP WHERE id = 1");
        update.bind(1, cfg.level);
        update.bind(2, cfg.flushIntervalMs);
        update.exec();
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore setLoggerConfig failed: {}", error.what());
        return false;
    }
}

bool ConfigStore::getKafkaConfig(KafkaRuntimeConfig& out) const {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) {
            return false;
        }
        SQLite::Statement query(*db_,
            "SELECT brokers, topics, group_id, sasl_username, sasl_password, ssl_ca_location, "
            "ssl_key_location, ssl_certificate_location, security_protocol, sasl_mechanism, "
            "ssl_endpoint_identification_algorithm FROM kafka_config WHERE id = 1");
        if (!query.executeStep()) {
            return false;
        }
        out.brokers = query.getColumn(0).getText();
        out.groupId = query.getColumn(2).getText();
        out.saslUsername = query.getColumn(3).getText();
        out.saslPassword = query.getColumn(4).getText();
        out.sslCaLocation = query.getColumn(5).getText();
        out.sslKeyLocation = query.getColumn(6).getText();
        out.sslCertificateLocation = query.getColumn(7).getText();
        out.securityProtocol = query.getColumn(8).getText();
        out.saslMechanism = query.getColumn(9).getText();
        out.sslEndpointIdentificationAlgorithm = query.getColumn(10).getText();

        const auto parsed = nlohmann::json::parse(query.getColumn(1).getText(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_array()) {
            return false;
        }
        out.topics = parsed.get<std::vector<std::string>>();
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore getKafkaConfig failed: {}", error.what());
        return false;
    }
}

bool ConfigStore::setKafkaConfig(const KafkaRuntimeConfig& cfg) {
    try {
        const nlohmann::json topics = cfg.topics;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) {
            return false;
        }
        SQLite::Statement update(*db_,
            "UPDATE kafka_config SET brokers = ?, topics = ?, group_id = ?, sasl_username = ?, "
            "sasl_password = ?, ssl_ca_location = ?, ssl_key_location = ?, "
            "ssl_certificate_location = ?, security_protocol = ?, sasl_mechanism = ?, "
            "ssl_endpoint_identification_algorithm = ?, updated_at = CURRENT_TIMESTAMP "
            "WHERE id = 1");
        update.bind(1, cfg.brokers);
        update.bind(2, topics.dump());
        update.bind(3, cfg.groupId);
        update.bind(4, cfg.saslUsername);
        update.bind(5, cfg.saslPassword);
        update.bind(6, cfg.sslCaLocation);
        update.bind(7, cfg.sslKeyLocation);
        update.bind(8, cfg.sslCertificateLocation);
        update.bind(9, cfg.securityProtocol);
        update.bind(10, cfg.saslMechanism);
        update.bind(11, cfg.sslEndpointIdentificationAlgorithm);
        update.exec();
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore setKafkaConfig failed: {}", error.what());
        return false;
    }
}

} // namespace MQProxy
