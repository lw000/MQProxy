#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

namespace MQProxy {

/**
 * @brief 日志单例；initialize() 成功前所有输出静默丢弃。
 *
 * 运行期可动态修改级别与定时刷新间隔；刷新线程在 shutdown() 中回收。
 */
class Logger {
public:
    static Logger& getInstance();

    /**
     * @brief 初始化控制台 + 按天文件双 sink。
     * @return 成功 true；级别越界或 sink 创建失败返回 false。
     */
    bool initialize(const std::string& outdir, int level, int flushIntervalMs);

    /** @brief 进程退出前停止刷新线程、落盘并释放 sink；幂等。 */
    void shutdown();

    /** @brief 运行期修改级别，0=trace..5=critical；越界返回 false。 */
    bool setLevel(int level);

    /** @brief 运行期修改刷新间隔（毫秒）；0=关闭定时刷新；越界返回 false。 */
    bool setFlushIntervalMs(int intervalMs);

    int level() const { return level_.load(); }
    int flushIntervalMs() const { return flushIntervalMs_.load(); }

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

    void stopFlushThread();
    void flushLoop(std::shared_ptr<spdlog::logger> logger);

    std::shared_ptr<spdlog::logger> logger_;
    std::atomic<int> level_{1};
    std::atomic<int> flushIntervalMs_{1000};

    std::mutex flushMutex_;
    std::condition_variable flushCv_;
    std::atomic<bool> flushRunning_{false};
    std::thread flushThread_;
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
