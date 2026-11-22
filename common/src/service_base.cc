#include "common/service_base.hxx"

#include "common/logger.hxx"

namespace MQProxy {
namespace {

const char* stateName(ServiceState state) {
    switch (state) {
        case ServiceState::kUninitialized: return "kUninitialized";
        case ServiceState::kInitialized:   return "kInitialized";
        case ServiceState::kRunning:       return "kRunning";
        case ServiceState::kStopped:       return "kStopped";
        case ServiceState::kFailed:        return "kFailed";
    }
    return "unknown";
}

} // namespace

bool ServiceBase::init(const ServiceConfig& cfg) {
    if (state_.load() != ServiceState::kUninitialized) {
        LOG_WARN_FMT("service {}({}) init rejected, state={}", id(), name(), stateName(state_.load()));
        return false;
    }
    config_ = cfg;
    if (!on_init(cfg)) {
        state_.store(ServiceState::kFailed);
        LOG_ERROR_FMT("service {}({}) init failed", id(), name());
        return false;
    }
    state_.store(ServiceState::kInitialized);
    LOG_INFO_FMT("service {}({}) state: kUninitialized -> kInitialized", id(), name());
    return true;
}

bool ServiceBase::start() {
    if (state_.load() != ServiceState::kInitialized) {
        LOG_WARN_FMT("service {}({}) start rejected, state={}", id(), name(), stateName(state_.load()));
        return false;
    }
    if (!on_start()) {
        state_.store(ServiceState::kFailed);
        LOG_ERROR_FMT("service {}({}) start failed", id(), name());
        return false;
    }
    state_.store(ServiceState::kRunning);
    LOG_INFO_FMT("service {}({}) state: kInitialized -> kRunning", id(), name());
    return true;
}

void ServiceBase::stop() noexcept {
    const ServiceState current = state_.load();
    if (current == ServiceState::kStopped || current == ServiceState::kUninitialized) {
        return;
    }
    on_stop();
    state_.store(ServiceState::kStopped);
    LOG_INFO_FMT("service {}({}) state: {} -> kStopped", id(), name(), stateName(current));
}

bool ServiceBase::restart() {
    const ServiceState current = state_.load();
    if (current == ServiceState::kUninitialized || current == ServiceState::kFailed) {
        LOG_WARN_FMT("service {}({}) restart rejected, state={}", id(), name(), stateName(current));
        return false;
    }
    if (current == ServiceState::kRunning) {
        on_stop();
    }
    const ServiceConfig cfg = config_;
    state_.store(ServiceState::kUninitialized);
    LOG_INFO_FMT("service {}({}) restarting", id(), name());
    return init(cfg) && start();
}

} // namespace MQProxy
