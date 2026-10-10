#include "Logger.hxx"
#include <iostream>

namespace MQProxy {

	Logger& Logger::getInstance() {
		static Logger instance;
		return instance;
	}

	void Logger::initialize(const std::string& outdir, int level) {
		try {
			// 创建日志目录
			std::vector<spdlog::sink_ptr> sinks;

			// 控制台输出
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink->set_level(spdlog::level::trace);
			sinks.push_back(console_sink);

			// 文件输出（按天）
			auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
				outdir + "/log.txt", 0, 0);
			file_sink->set_level(spdlog::level::trace);
			sinks.push_back(file_sink);

			// 创建logger
			logger_ = std::make_shared<spdlog::logger>("MQProxy", sinks.begin(), sinks.end());
			logger_->set_level(static_cast<spdlog::level::level_enum>(level));
			logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
			logger_->flush_on(spdlog::level::info);

			spdlog::set_default_logger(logger_);

		}
		catch (const spdlog::spdlog_ex& ex) {
			std::cerr << "Log init failed: " << ex.what() << std::endl;
		}
	}

	void Logger::trace(const std::string& msg) {
		if (logger_) {
			logger_->trace(msg);
		}
	}

	void Logger::debug(const std::string& msg) {
		if (logger_) {
			logger_->debug(msg);
		}
	}

	void Logger::info(const std::string& msg) {
		if (logger_) {
			logger_->info(msg);
		}
	}

	void Logger::warn(const std::string& msg) {
		if (logger_) {
			logger_->warn(msg);
		}
	}

	void Logger::error(const std::string& msg) {
		if (logger_) {
			logger_->error(msg);
		}
	}

	void Logger::critical(const std::string& msg) {
		if (logger_) {
			logger_->critical(msg);
		}
	}

} // namespace MQProxy
