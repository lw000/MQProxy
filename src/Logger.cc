#include "Logger.hxx"

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <vector>

namespace MQProxy {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

bool Logger::initialize(const std::string& outdir, int level) {
    if (logger_) {
        std::cerr << "Logger: already initialized" << std::endl;
        return false;
    }
    if (level < static_cast<int>(spdlog::level::trace) ||
        level > static_cast<int>(spdlog::level::critical)) {
        std::cerr << "Logger: level out of range: " << level << std::endl;
        return false;
    }
    try {
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
        sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(
            outdir + "/log.txt", 0, 0));
        for (const auto& sink : sinks) {
            sink->set_level(spdlog::level::trace);
        }

        logger_ = std::make_shared<spdlog::logger>("MQProxy", sinks.begin(), sinks.end());
        logger_->set_level(static_cast<spdlog::level::level_enum>(level));
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
        logger_->flush_on(spdlog::level::info);
        spdlog::set_default_logger(logger_);
        return true;
    }
    catch (const spdlog::spdlog_ex& error) {
        std::cerr << "Logger: init failed: " << error.what() << std::endl;
        logger_.reset();
        return false;
    }
}

void Logger::shutdown() {
    if (!logger_) {
        return;
    }
    logger_->flush();
    spdlog::set_default_logger(nullptr);
    logger_.reset();
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
