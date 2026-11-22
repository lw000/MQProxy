#include "Config.hxx"
#include "KafkaConsumer.hxx"
#include "Logger.hxx"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MQProxy {

volatile std::sig_atomic_t g_running = 1;

void signalHandler(int signalNumber) {
    g_running = 0;
    (void)signalNumber;
}

void setupSignalHandlers() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    std::signal(SIGBREAK, signalHandler);
#endif
}

void printBanner(const Config& config) {
    std::cout << "\n========================================\n"
              << "  " << config.getServiceName() << "\n"
              << "========================================\n"
              << "Mode: " << config.getMode() << "\n"
              << "Environment: " << (config.isEnableProd() ? "PRODUCTION" : "DEVELOPMENT")
              << "\n========================================\n\n";
}

} // namespace MQProxy

int main(int argc, char* argv[]) {
    using namespace MQProxy;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [config.toml]" << std::endl;
        return 1;
    }
    const std::string configFile = argc == 2 ? argv[1] : "config.toml";

    auto& config = Config::getInstance();
    if (!config.load(configFile)) {
        std::cerr << "Failed to load configuration: " << configFile << std::endl;
        return 1;
    }

    const auto& loggerConfig = config.getLoggerConfig();
    if (!Logger::getInstance().initialize(loggerConfig.outdir, loggerConfig.level)) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return 1;
    }

    LOG_INFO("========================================");
    LOG_INFO("Application starting...");
    LOG_INFO_FMT("Config file: {}", configFile);
    LOG_INFO("========================================");
    printBanner(config);

    setupSignalHandlers();

    KafkaConsumer consumer;
    if (!consumer.initialize(config)) {
        LOG_ERROR("Failed to initialize Kafka consumer");
        Logger::getInstance().shutdown();
        return 1;
    }

    consumer.setMessageCallback([](const std::string& topic, int partition, int64_t offset,
                                   const std::string& key, const std::string& payload) {
        LOG_INFO_FMT("Received message - Topic: {}, Partition: {}, Offset: {}",
                     topic, partition, offset);
        if (!key.empty()) {
            LOG_DEBUG_FMT("Key: {}", key);
        }
        if (Config::getInstance().isShowContent()) {
            LOG_DEBUG_FMT("Payload: {}", payload);
        }
        else {
            LOG_DEBUG_FMT("Payload length: {} bytes", payload.size());
        }
    });

    if (!consumer.subscribe(config.getKafkaConfig().topics)) {
        LOG_ERROR("Failed to subscribe to topics");
        consumer.stop();
        Logger::getInstance().shutdown();
        return 1;
    }
    consumer.start();
    LOG_INFO("Application is running. Press Ctrl+C to stop.");

    while (g_running != 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!consumer.isRunning()) {
            LOG_ERROR("Kafka consumer stopped unexpectedly");
            break;
        }
    }

    consumer.stop();
    LOG_INFO("Application stopped successfully");
    Logger::getInstance().shutdown();
    return 0;
}
