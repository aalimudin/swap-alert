#pragma once

#include <functional>

enum class SystemEvent {
    WillSleep,
    DidWake,
    SessionInactive,
    SessionActive,
    DesktopRestored,
    WillPowerOff,
};

using SystemEventCallback = std::function<void(SystemEvent)>;

class ISystemEventMonitor {
public:
    virtual ~ISystemEventMonitor() = default;
    virtual void start(SystemEventCallback callback) = 0;
    virtual void stop() = 0;
};
