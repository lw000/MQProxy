#pragma once

#include <memory>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace MQProxy {

/**
 * @brief 日志单例；initialize() 成功前所有输出静默丢弃。
 */
class Logger {
public:
    static Logger& getInstance();

    /**
     * @brief 初始化控制台 + 按天文件双 sink。
     * @return 成功 true；level 越界或 sink 创建失败返回 false。
     */
    bool initialize(const std::string& outdir, int level);

    /** @brief 进程退出前落盘并释放 sink；幂等。 */
    void shutdown();

    void trace(const std::string& msg);
    void debug(const std::string& msg);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void critical(const std::string& msg);

    template <typename... Args>
    void traceFmt(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->trace(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void debugFmt(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->debug(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void infoFmt(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->info(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void warnFmt(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->warn(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void errorFmt(const std::string& fmt, Args&&... args) {
        if (logger_) {
            logger_->error(fmt, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
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
