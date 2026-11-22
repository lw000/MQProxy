#include "common/service_manager.hxx"

#include "common/logger.hxx"

namespace MQProxy {

bool ServiceManager::registerService(std::shared_ptr<ServiceBase> service) {
    if (!service || service->id().empty()) {
        LOG_ERROR("service registration rejected: null service or empty id");
        return false;
    }
    if (byId_.find(service->id()) != byId_.end()) {
        LOG_ERROR_FMT("service registration rejected: duplicate id {}", service->id());
        return false;
    }
    byId_.emplace(service->id(), service);
    services_.push_back(std::move(service));
    LOG_INFO_FMT("service registered: {}", services_.back()->id());
    return true;
}

bool ServiceManager::initAll(const ServiceConfig& cfg) {
    for (const auto& service : services_) {
        if (!service->init(cfg)) {
            LOG_ERROR_FMT("init_all aborted at {}", service->id());
            return false;
        }
    }
    return true;
}

bool ServiceManager::startAll() {
    for (const auto& service : services_) {
        if (!service->start()) {
            LOG_ERROR_FMT("start_all aborted at {}", service->id());
            return false;
        }
    }
    return true;
}

void ServiceManager::stopAll() noexcept {
    for (auto it = services_.rbegin(); it != services_.rend(); ++it) {
        (*it)->stop();
    }
}

} // namespace MQProxy
