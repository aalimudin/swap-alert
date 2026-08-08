#include "platform/macos/MacAutostartService.hpp"

#include "core/Logging.hpp"

#import <ServiceManagement/ServiceManagement.h>

bool MacAutostartService::isEnabled() const
{
    if (@available(macOS 13.0, *)) {
        const bool enabled = [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
        qCInfo(logSystem) << "Start-at-login status is" << (enabled ? "enabled" : "disabled");
        return enabled;
    }
    return false;
}

bool MacAutostartService::setEnabled(bool enabled, QString& errorMessage)
{
    if (@available(macOS 13.0, *)) {
        NSError* error = nil;
        const BOOL succeeded = enabled
            ? [[SMAppService mainAppService] registerAndReturnError:&error]
            : [[SMAppService mainAppService] unregisterAndReturnError:&error];
        if (!succeeded) {
            errorMessage = QString::fromUtf8(error.localizedDescription.UTF8String);
            qCWarning(logSystem) << "Unable to update start-at-login:" << errorMessage;
            return false;
        }
        qCInfo(logSystem) << "Start at login" << (enabled ? "enabled" : "disabled");
        return true;
    }

    errorMessage = QStringLiteral("Start at login requires macOS 13 or later.");
    qCWarning(logSystem) << errorMessage;
    return false;
}
