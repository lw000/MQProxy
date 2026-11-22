#include "common/bootstrap_config.hxx"
#include "common/logger.hxx"
#include "common/service_manager.hxx"
#include "config_store/config_store.hxx"
#include "kafka_manager/kafka_manager.hxx"
#include "web_server/web_server.hxx"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
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

void printBanner(const BootstrapConfig& config) {
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

    BootstrapConfig config;
    if (!config.load(configFile)) {
        std::cerr << "Failed to load configuration: " << configFile << std::endl;
        return 1;
    }

    const ServiceConfig serviceConfig = config.toServiceConfig();
    Logger& logger = Logger::getInstance();
    if (!logger.initialize(serviceConfig.logOutdir, serviceConfig.logLevel,
                           serviceConfig.logFlushIntervalMs)) {
        std::cerr << "Failed to initialize logger" << std::endl;
        return 1;
    }

    LOG_INFO("========================================");
    LOG_INFO("Application starting...");
    LOG_INFO_FMT("Config file: {}", configFile);
    LOG_INFO("========================================");
    printBanner(config);

    setupSignalHandlers();

    auto store = std::make_shared<ConfigStore>(config);
    auto kafka = std::make_shared<KafkaManager>(*store);
    auto web = std::make_shared<WebServer>(*store, *kafka, logger);
    kafka->setShowContent(config.isShowContent());

    ServiceManager manager;
    if (!manager.registerService(store) || !manager.registerService(kafka) ||
        !manager.registerService(web)) {
        manager.stopAll();
        logger.shutdown();
        return 1;
    }
    if (!manager.initAll(serviceConfig)) {
        manager.stopAll();
        LOG_ERROR("Application failed to initialize");
        logger.shutdown();
        return 1;
    }

    // 库内 logger 配置为权威值：进程启动时先按启动依赖初始化，再应用已持久化的运行值
    LoggerRuntimeConfig persistedLogger;
    if (store->getLoggerConfig(persistedLogger)) {
        logger.setLevel(persistedLogger.level);
        logger.setFlushIntervalMs(persistedLogger.flushIntervalMs);
    }

    if (!manager.startAll()) {
        manager.stopAll();
        LOG_ERROR("Application failed to start");
        logger.shutdown();
        return 1;
    }

    LOG_INFO_FMT("Web console at http://{}:{}/", serviceConfig.httpHost, serviceConfig.httpPort);
    LOG_INFO("Application is running. Press Ctrl+C to stop.");

    while (g_running != 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    manager.stopAll();
    LOG_INFO("Application stopped successfully");
    logger.shutdown();
    return 0;
}
