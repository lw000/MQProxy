#pragma once

#include <atomic>
#include <string>

namespace MQProxy {

/**
 * @brief 启动依赖配置：仅含进程启动必需项，动态配置存于 SQLite。
 */
struct ServiceConfig {
    std::string dbPath = "mqproxy.db";       ///< SQLite 动态配置库路径
    std::string httpHost = "127.0.0.1";      ///< Web 监听地址
    int httpPort = 8080;                     ///< Web 监听端口
    std::string webRoot = "web";             ///< 静态管理页目录
    std::string logOutdir = "logs";          ///< 日志输出目录
    int logLevel = 1;                        ///< 初始日志级别 0=trace..5=critical
    int logFlushIntervalMs = 1000;           ///< 初始日志刷新间隔，0=关闭定时刷新
};

/** @brief 子服务状态；合法迁移：未初始化→已初始化→运行中→已停止，任意态→失败。 */
enum class ServiceState { kUninitialized, kInitialized, kRunning, kStopped, kFailed };

/**
 * @brief 子服务公共基类（六要素：init/start/stop/name/id/state）。
 *
 * 生命周期入口非虚，子类只能覆写 protected 钩子；构造不做事。
 */
class ServiceBase {
public:
    virtual ~ServiceBase() = default;

    /** @brief 一次性资源准备；失败置 kFailed。 */
    bool init(const ServiceConfig& cfg);

    /** @brief 启动运行线程/监听；仅允许 kInitialized→kRunning。 */
    bool start();

    /** @brief 停止并释放资源；幂等、noexcept。 */
    void stop() noexcept;

    /** @brief 服务级重启：停止→重新 init→start，用于加载最新动态配置。 */
    bool restart();

    virtual const char* name() const = 0;
    virtual const std::string& id() const = 0;
    ServiceState state() const { return state_.load(); }

protected:
    virtual bool on_init(const ServiceConfig& cfg) = 0;
    virtual bool on_start() = 0;
    virtual void on_stop() noexcept = 0;

    const ServiceConfig& config() const { return config_; }

private:
    ServiceConfig config_;
    std::atomic<ServiceState> state_{ServiceState::kUninitialized};
};

} // namespace MQProxy
