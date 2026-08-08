#pragma once

#include "platform/ISystemEventMonitor.hpp"

#include <memory>

class LinuxSystemEventMonitorImpl;

class LinuxSystemEventMonitor final : public ISystemEventMonitor {
public:
    LinuxSystemEventMonitor();
    ~LinuxSystemEventMonitor() override;

    void start(SystemEventCallback callback) override;
    void stop() override;

private:
    std::unique_ptr<LinuxSystemEventMonitorImpl> m_impl;
};
