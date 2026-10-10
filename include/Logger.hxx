#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <vector>

namespace MQProxy {

	class Logger {
	public:
		static Logger& getInstance();

		void initialize(const std::string& outdir, int level);

		void trace(const std::string& msg);
		void debug(const std::string& msg);
		void info(const std::string& msg);
		void warn(const std::string& msg);
		void error(const std::string& msg);
		void critical(const std::string& msg);

		template<typename... Args>
		void traceFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->trace(fmt, std::forward<Args>(args)...);
			}
		}

		template<typename... Args>
		void debugFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->debug(fmt, std::forward<Args>(args)...);
			}
		}

		template<typename... Args>
		void infoFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->info(fmt, std::forward<Args>(args)...);
			}
		}

		template<typename... Args>
		void warnFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->warn(fmt, std::forward<Args>(args)...);
			}
		}

		template<typename... Args>
		void errorFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->error(fmt, std::forward<Args>(args)...);
			}
		}

		template<typename... Args>
		void criticalFmt(const std::string& fmt, Args&&... args) {
			if (logger_) {
				logger_->critical(fmt, std::forward<Args>(args)...);
			}
		}

	private:
		Logger() = default;
		~Logger() = default;
		Logger(const Logger&) = delete;
		Logger& operator=(const Logger&) = delete;

		std::shared_ptr<spdlog::logger> logger_;
	};

} // namespace MQProxy

// 便捷宏
#define LOG_TRACE(msg) MQProxy::Logger::getInstance().trace(msg)
#define LOG_DEBUG(msg) MQProxy::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) MQProxy::Logger::getInstance().info(msg)
#define LOG_WARN(msg) MQProxy::Logger::getInstance().warn(msg)
#define LOG_ERROR(msg) MQProxy::Logger::getInstance().error(msg)
#define LOG_CRITICAL(msg) MQProxy::Logger::getInstance().critical(msg)

#define LOG_TRACE_FMT(fmt, ...) MQProxy::Logger::getInstance().traceFmt(fmt, __VA_ARGS__)
#define LOG_DEBUG_FMT(fmt, ...) MQProxy::Logger::getInstance().debugFmt(fmt, __VA_ARGS__)
#define LOG_INFO_FMT(fmt, ...) MQProxy::Logger::getInstance().infoFmt(fmt, __VA_ARGS__)
#define LOG_WARN_FMT(fmt, ...) MQProxy::Logger::getInstance().warnFmt(fmt, __VA_ARGS__)
#define LOG_ERROR_FMT(fmt, ...) MQProxy::Logger::getInstance().errorFmt(fmt, __VA_ARGS__)
#define LOG_CRITICAL_FMT(fmt, ...) MQProxy::Logger::getInstance().criticalFmt(fmt, __VA_ARGS__)
