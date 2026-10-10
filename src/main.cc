#include "Config.hxx"
#include "Logger.hxx"
#include "KafkaConsumer.hxx"
#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace MQProxy {

	std::atomic<bool> g_running(true);

	void signalHandler(int signal) {
		LOG_INFO_FMT("Received signal {}, shutting down...", signal);
		g_running = false;
	}

	void setupSignalHandlers() {
		std::signal(SIGINT, signalHandler);
		std::signal(SIGTERM, signalHandler);

#ifdef _WIN32
		// Windows特定的信号处理
		std::signal(SIGBREAK, signalHandler);
#endif
	}

	void printBanner(const Config& config) {
		std::cout << "\n";
		std::cout << "========================================\n";
		std::cout << "  " << config.getServiceName() << "\n";
		std::cout << "========================================\n";
		std::cout << "Mode: " << config.getMode() << "\n";
		std::cout << "Environment: " << (config.isEnableProd() ? "PRODUCTION" : "DEVELOPMENT") << "\n";
		std::cout << "========================================\n";
		std::cout << "\n";
	}

} // namespace MQProxy

int main(int argc, char* argv[]) {
	using namespace MQProxy;

#ifdef _WIN32
	// 设置控制台代码页为UTF-8
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif

	// 确定配置文件路径
	std::string configFile = "config.toml";
	if (argc > 1) {
		configFile = argv[1];
	}

	// 加载配置
	auto& config = Config::getInstance();
	if (!config.load(configFile)) {
		std::cerr << "Failed to load configuration from: " << configFile << std::endl;
		return 1;
	}

	// 初始化日志
	const auto& loggerConfig = config.getLoggerConfig();
	Logger::getInstance().initialize(loggerConfig.outdir, loggerConfig.level);

	LOG_INFO("========================================");
	LOG_INFO("Application starting...");
	LOG_INFO_FMT("Config file: {}", configFile);
	LOG_INFO("========================================");

	// 打印启动信息
	printBanner(config);

	// 设置信号处理器
	setupSignalHandlers();

	// 创建并初始化Kafka消费者
	KafkaConsumer consumer;
	if (!consumer.initialize(config)) {
		LOG_ERROR("Failed to initialize Kafka consumer");
		return 1;
	}

	// 设置消息回调
	consumer.setMessageCallback([](const std::string& topic,
		int partition,
		int64_t offset,
		const std::string& key,
		const std::string& payload) {
			LOG_INFO_FMT("Received message - Topic: {}, Partition: {}, Offset: {}",
				topic, partition, offset);

			if (!key.empty()) {
				LOG_DEBUG_FMT("Key: {}", key);
			}

			if (Config::getInstance().isShowContent()) {
				LOG_DEBUG_FMT("Payload: {}", payload);
			}
			else {
				LOG_DEBUG_FMT("Payload length: {} bytes", payload.length());
			}
		});

	// 订阅主题
	const auto& kafkaConfig = config.getKafkaConfig();
	if (!consumer.subscribe(kafkaConfig.topics)) {
		LOG_ERROR("Failed to subscribe to topics");
		return 1;
	}

	// 启动消费者
	consumer.start();

	LOG_INFO("Application is running. Press Ctrl+C to stop.");

	// 主循环
	int checkInterval = 5; // 秒
	while (g_running) {
		std::this_thread::sleep_for(std::chrono::seconds(checkInterval));

		if (!consumer.isRunning()) {
			LOG_ERROR("Kafka consumer stopped unexpectedly");
			g_running = false;
		}
	}

	// 停止消费者
	LOG_INFO("Stopping Kafka consumer...");
	consumer.stop();

	LOG_INFO("========================================");
	LOG_INFO("Application stopped successfully");
	LOG_INFO("========================================");

	return 0;
}
