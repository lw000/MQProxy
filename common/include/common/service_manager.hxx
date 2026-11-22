#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/service_base.hxx"

namespace MQProxy {

/**
 * @brief 子服务注册与统一编排点；stop_all 按注册逆序执行。
 */
class ServiceManager {
public:
    /** @brief 注册服务；id 重复或空指针返回 false。 */
    bool registerService(std::shared_ptr<ServiceBase> service);

    /** @brief 按注册顺序初始化；任一失败即中止并返回 false。 */
    bool initAll(const ServiceConfig& cfg);

    /** @brief 按注册顺序启动；任一失败即中止并返回 false。 */
    bool startAll();

    /** @brief 按注册逆序停止；幂等。 */
    void stopAll() noexcept;

    std::size_t size() const { return services_.size(); }

private:
    std::vector<std::shared_ptr<ServiceBase>> services_;
    std::unordered_map<std::string, std::shared_ptr<ServiceBase>> byId_;
};

} // namespace MQProxy
