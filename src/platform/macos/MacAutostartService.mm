#include "platform/macos/MacAutostartService.hpp"

#import <ServiceManagement/ServiceManagement.h>

bool MacAutostartService::isEnabled() const
{
    if (@available(macOS 13.0, *)) {
        return [SMAppService mainAppService].status == SMAppServiceStatusEnabled;
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
            return false;
        }
        return true;
    }

    errorMessage = QStringLiteral("Start at login requires macOS 13 or later.");
    return false;
}
