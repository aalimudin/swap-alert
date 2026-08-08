#pragma once

#include "platform/ISystemEventMonitor.hpp"

#include <memory>

class MacSystemEventMonitor final : public ISystemEventMonitor {
public:
    MacSystemEventMonitor();
    ~MacSystemEventMonitor() override;

    void start(SystemEventCallback callback) override;
    void stop() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
