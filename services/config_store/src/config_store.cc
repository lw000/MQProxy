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
    SQLite::Statement loggerCount(*db_, "SELECT COUNT(*) AS row_count FROM logger_config");
    if (loggerCount.executeStep() && loggerCount.getColumn("row_count").getInt() == 0) {
        SQLite::Statement insert(*db_,
            "INSERT INTO logger_config(id, level, flush_interval_ms) "
            "VALUES(1, :level, :flush_interval_ms)");
        insert.bind(":level", loggerSeed.level);
        insert.bind(":flush_interval_ms", loggerSeed.flushIntervalMs);
        insert.exec();
    }

    SQLite::Statement kafkaCount(*db_, "SELECT COUNT(*) AS row_count FROM kafka_config");
    if (kafkaCount.executeStep() && kafkaCount.getColumn("row_count").getInt() == 0) {
        const KafkaRuntimeConfig kafkaSeed = bootstrap_.getKafkaSeed();
        const nlohmann::json topics = kafkaSeed.topics;
        SQLite::Statement insert(*db_,
            "INSERT INTO kafka_config(id, brokers, topics, group_id, sasl_username, sasl_password, "
            "ssl_ca_location, ssl_key_location, ssl_certificate_location, security_protocol, "
            "sasl_mechanism, ssl_endpoint_identification_algorithm) "
            "VALUES(1, :brokers, :topics, :group_id, :sasl_username, :sasl_password, "
            ":ssl_ca_location, :ssl_key_location, :ssl_certificate_location, :security_protocol, "
            ":sasl_mechanism, :ssl_endpoint_identification_algorithm)");
        insert.bind(":brokers", kafkaSeed.brokers);
        insert.bind(":topics", topics.dump());
        insert.bind(":group_id", kafkaSeed.groupId);
        insert.bind(":sasl_username", kafkaSeed.saslUsername);
        insert.bind(":sasl_password", kafkaSeed.saslPassword);
        insert.bind(":ssl_ca_location", kafkaSeed.sslCaLocation);
        insert.bind(":ssl_key_location", kafkaSeed.sslKeyLocation);
        insert.bind(":ssl_certificate_location", kafkaSeed.sslCertificateLocation);
        insert.bind(":security_protocol", kafkaSeed.securityProtocol);
        insert.bind(":sasl_mechanism", kafkaSeed.saslMechanism);
        insert.bind(":ssl_endpoint_identification_algorithm",
                    kafkaSeed.sslEndpointIdentificationAlgorithm);
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
        out.level = query.getColumn("level").getInt();
        out.flushIntervalMs = query.getColumn("flush_interval_ms").getInt();
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
            "UPDATE logger_config SET level = :level, flush_interval_ms = :flush_interval_ms, "
            "updated_at = CURRENT_TIMESTAMP WHERE id = 1");
        update.bind(":level", cfg.level);
        update.bind(":flush_interval_ms", cfg.flushIntervalMs);
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
        out.brokers = query.getColumn("brokers").getText();
        out.groupId = query.getColumn("group_id").getText();
        out.saslUsername = query.getColumn("sasl_username").getText();
        out.saslPassword = query.getColumn("sasl_password").getText();
        out.sslCaLocation = query.getColumn("ssl_ca_location").getText();
        out.sslKeyLocation = query.getColumn("ssl_key_location").getText();
        out.sslCertificateLocation = query.getColumn("ssl_certificate_location").getText();
        out.securityProtocol = query.getColumn("security_protocol").getText();
        out.saslMechanism = query.getColumn("sasl_mechanism").getText();
        out.sslEndpointIdentificationAlgorithm =
            query.getColumn("ssl_endpoint_identification_algorithm").getText();

        const auto parsed = nlohmann::json::parse(
            query.getColumn("topics").getText(), nullptr, false);
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
            "UPDATE kafka_config SET brokers = :brokers, topics = :topics, "
            "group_id = :group_id, sasl_username = :sasl_username, "
            "sasl_password = :sasl_password, ssl_ca_location = :ssl_ca_location, "
            "ssl_key_location = :ssl_key_location, "
            "ssl_certificate_location = :ssl_certificate_location, "
            "security_protocol = :security_protocol, sasl_mechanism = :sasl_mechanism, "
            "ssl_endpoint_identification_algorithm = :ssl_endpoint_identification_algorithm, "
            "updated_at = CURRENT_TIMESTAMP WHERE id = 1");
        update.bind(":brokers", cfg.brokers);
        update.bind(":topics", topics.dump());
        update.bind(":group_id", cfg.groupId);
        update.bind(":sasl_username", cfg.saslUsername);
        update.bind(":sasl_password", cfg.saslPassword);
        update.bind(":ssl_ca_location", cfg.sslCaLocation);
        update.bind(":ssl_key_location", cfg.sslKeyLocation);
        update.bind(":ssl_certificate_location", cfg.sslCertificateLocation);
        update.bind(":security_protocol", cfg.securityProtocol);
        update.bind(":sasl_mechanism", cfg.saslMechanism);
        update.bind(":ssl_endpoint_identification_algorithm",
                    cfg.sslEndpointIdentificationAlgorithm);
        update.exec();
        return true;
    }
    catch (const std::exception& error) {
        LOG_ERROR_FMT("ConfigStore setKafkaConfig failed: {}", error.what());
        return false;
    }
}

} // namespace MQProxy
