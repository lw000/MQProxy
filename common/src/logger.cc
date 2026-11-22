#include "common/logger.hxx"

#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <iostream>
#include <vector>

namespace MQProxy {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

bool Logger::initialize(const std::string& outdir, int level, int flushIntervalMs) {
    if (logger_) {
        std::cerr << "Logger: already initialized" << std::endl;
        return false;
    }
    if (level < static_cast<int>(spdlog::level::trace) ||
        level > static_cast<int>(spdlog::level::critical) ||
        flushIntervalMs < 0) {
        std::cerr << "Logger: invalid level or flush interval" << std::endl;
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

        level_.store(level);
        setFlushIntervalMs(flushIntervalMs);
        LOG_INFO_FMT("Logger initialized, level={}, flushIntervalMs={}", level, flushIntervalMs);
        return true;
    }
    catch (const spdlog::spdlog_ex& error) {
        std::cerr << "Logger: init failed: " << error.what() << std::endl;
        logger_.reset();
        return false;
    }
}

void Logger::stopFlushThread() {
    {
        std::lock_guard<std::mutex> lock(flushMutex_);
        flushRunning_.store(false);
        flushCv_.notify_all();
    }
    if (flushThread_.joinable()) {
        flushThread_.join();
    }
}

void Logger::flushLoop(std::shared_ptr<spdlog::logger> logger) {
    std::unique_lock<std::mutex> lock(flushMutex_);
    while (flushRunning_.load()) {
        if (flushCv_.wait_for(lock,
                              std::chrono::milliseconds(flushIntervalMs_.load()),
                              [this] { return !flushRunning_.load(); })) {
            break;
        }
        if (logger) {
            logger->flush();
        }
    }
}

bool Logger::setLevel(int level) {
    if (level < static_cast<int>(spdlog::level::trace) ||
        level > static_cast<int>(spdlog::level::critical)) {
        LOG_WARN_FMT("Logger: reject invalid level {}", level);
        return false;
    }
    level_.store(level);
    if (logger_) {
        logger_->set_level(static_cast<spdlog::level::level_enum>(level));
    }
    LOG_INFO_FMT("Logger runtime config applied: level={}, flushIntervalMs={}",
                 level, flushIntervalMs_.load());
    return true;
}

bool Logger::setFlushIntervalMs(int intervalMs) {
    if (intervalMs < 0) {
        LOG_WARN_FMT("Logger: reject invalid flush interval {}", intervalMs);
        return false;
    }
    stopFlushThread();
    flushIntervalMs_.store(intervalMs);
    if (intervalMs > 0 && logger_) {
        std::shared_ptr<spdlog::logger> localLogger = logger_;
        flushRunning_.store(true);
        flushThread_ = std::thread(&Logger::flushLoop, this, localLogger);
    }
    LOG_INFO_FMT("Logger runtime config applied: level={}, flushIntervalMs={}",
                 level_.load(), intervalMs);
    return true;
}

void Logger::shutdown() {
    stopFlushThread();
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
