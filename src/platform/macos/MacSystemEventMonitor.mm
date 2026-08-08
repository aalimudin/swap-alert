#include "platform/macos/MacSystemEventMonitor.hpp"

#import <AppKit/AppKit.h>

#include <utility>

struct MacSystemEventMonitor::Impl {
    SystemEventCallback callback;
    NSMutableArray* observerTokens = [[NSMutableArray alloc] init];
};

namespace {
void emitEvent(auto* impl, SystemEvent event)
{
    if (impl->callback) {
        impl->callback(event);
    }
}

void addObserver(auto* impl, NSNotificationCenter* center,
    NSNotificationName name, SystemEvent event)
{
    id token = [center addObserverForName:name
                                   object:nil
                                    queue:[NSOperationQueue mainQueue]
                               usingBlock:^(NSNotification*) {
        emitEvent(impl, event);
    }];
    [impl->observerTokens addObject:token];
}
}

MacSystemEventMonitor::MacSystemEventMonitor()
    : m_impl(std::make_unique<Impl>())
{
}

MacSystemEventMonitor::~MacSystemEventMonitor()
{
    stop();
}

void MacSystemEventMonitor::start(SystemEventCallback callback)
{
    stop();
    m_impl->callback = std::move(callback);

    NSNotificationCenter* center = [NSWorkspace sharedWorkspace].notificationCenter;
    addObserver(m_impl.get(), center, NSWorkspaceWillSleepNotification, SystemEvent::WillSleep);
    addObserver(m_impl.get(), center, NSWorkspaceDidWakeNotification, SystemEvent::DidWake);
    addObserver(m_impl.get(), center, NSWorkspaceSessionDidResignActiveNotification,
        SystemEvent::SessionInactive);
    addObserver(m_impl.get(), center, NSWorkspaceSessionDidBecomeActiveNotification,
        SystemEvent::SessionActive);
    addObserver(m_impl.get(), center, NSWorkspaceWillPowerOffNotification,
        SystemEvent::WillPowerOff);

    id launchToken = [center addObserverForName:NSWorkspaceDidLaunchApplicationNotification
                                         object:nil
                                          queue:[NSOperationQueue mainQueue]
                                     usingBlock:^(NSNotification* notification) {
        NSRunningApplication* application = notification.userInfo[NSWorkspaceApplicationKey];
        NSString* identifier = application.bundleIdentifier.lowercaseString;
        if ([identifier isEqualToString:@"com.apple.systemuiserver"]
            || [identifier isEqualToString:@"com.apple.finder"]) {
            emitEvent(m_impl.get(), SystemEvent::DesktopRestored);
        }
    }];
    [m_impl->observerTokens addObject:launchToken];
}

void MacSystemEventMonitor::stop()
{
    NSNotificationCenter* center = [NSWorkspace sharedWorkspace].notificationCenter;
    for (id token in m_impl->observerTokens) {
        [center removeObserver:token];
    }
    [m_impl->observerTokens removeAllObjects];
    m_impl->callback = {};
}
