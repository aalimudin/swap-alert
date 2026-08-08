#include "platform/macos/MacNotificationService.hpp"

#include "core/Logging.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#import <AppKit/AppKit.h>
#import <UserNotifications/UserNotifications.h>

@interface SwapAlertNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end


@implementation SwapAlertNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions options))completionHandler
{
    Q_UNUSED(center)
    Q_UNUSED(notification)
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionSound);
}
@end


namespace {
NSString* toNSString(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return [NSString stringWithUTF8String:utf8.constData()];
}

SwapAlertNotificationDelegate* notificationDelegate()
{
    static SwapAlertNotificationDelegate* delegate = [[SwapAlertNotificationDelegate alloc] init];
    return delegate;
}

NotificationAuthorizationStatus mapAuthorizationStatus(UNAuthorizationStatus status)
{
    switch (status) {
    case UNAuthorizationStatusNotDetermined:
        return NotificationAuthorizationStatus::NotDetermined;
    case UNAuthorizationStatusDenied:
        return NotificationAuthorizationStatus::Denied;
    case UNAuthorizationStatusAuthorized:
    case UNAuthorizationStatusProvisional:
        return NotificationAuthorizationStatus::Authorized;
    default:
        return NotificationAuthorizationStatus::Unknown;
    }
}

void completeOnMain(NotificationCallback callback, NotificationResult result)
{
    if (!callback) {
        return;
    }

    auto completion = [callback = std::move(callback), result = std::move(result)] {
        callback(result);
    };
    if (QCoreApplication::instance()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), std::move(completion),
            Qt::QueuedConnection);
    } else {
        completion();
    }
}
}

void MacNotificationService::authorizationStatus(NotificationCallback callback)
{
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = notificationDelegate();
    [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings* settings) {
        const auto status = mapAuthorizationStatus(settings.authorizationStatus);
        completeOnMain(std::move(callback), {status == NotificationAuthorizationStatus::Authorized,
                                                status, {}});
    }];
}

void MacNotificationService::requestAuthorization(NotificationCallback callback)
{
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = notificationDelegate();
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError* error) {
        if (error) {
            qCWarning(logNotifications) << "Notification authorization request failed:"
                                         << error.localizedDescription.UTF8String;
            completeOnMain(std::move(callback), {false, NotificationAuthorizationStatus::Unknown,
                                                    QString::fromUtf8(error.localizedDescription.UTF8String)});
            return;
        }

        const auto status = granted ? NotificationAuthorizationStatus::Authorized
                                    : NotificationAuthorizationStatus::Denied;
        qCInfo(logNotifications) << "Notification authorization"
                                  << (granted ? "granted" : "denied");
        completeOnMain(std::move(callback), {granted, status,
                                                granted ? QString()
                                                        : QStringLiteral("Notifications are disabled for Swap Alert.")});
    }];
}

void MacNotificationService::send(const QString& title, const QString& body, AlertTier tier,
    NotificationCallback callback)
{
    authorizationStatus(
        [this, title, body, tier, callback = std::move(callback)](const NotificationResult& statusResult) mutable {
            if (statusResult.authorization == NotificationAuthorizationStatus::NotDetermined) {
                requestAuthorization(
                    [this, title, body, tier, callback = std::move(callback)](
                        const NotificationResult& authorizationResult) mutable {
                        if (!authorizationResult.success) {
                            if (callback) {
                                callback(authorizationResult);
                            }
                            return;
                        }
                        send(title, body, tier, std::move(callback));
                    });
                return;
            }

            if (statusResult.authorization != NotificationAuthorizationStatus::Authorized) {
                if (callback) {
                    callback({false, statusResult.authorization,
                        statusResult.message.isEmpty()
                            ? QStringLiteral("Notifications are unavailable. Enable them in System Settings.")
                            : statusResult.message});
                }
                return;
            }

            UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
            content.title = toNSString(title);
            content.body = toNSString(body);
            content.sound = tier == AlertTier::Tier1 ? nil : [UNNotificationSound defaultSound];

            NSString* identifier = [[NSUUID UUID] UUIDString];
            UNNotificationRequest* request =
                [UNNotificationRequest requestWithIdentifier:identifier content:content trigger:nil];
            [[UNUserNotificationCenter currentNotificationCenter]
                addNotificationRequest:request
                 withCompletionHandler:^(NSError* error) {
                if (error) {
                    qCWarning(logNotifications) << "Notification delivery failed:"
                                                 << error.localizedDescription.UTF8String;
                    completeOnMain(std::move(callback),
                        {false, NotificationAuthorizationStatus::Authorized,
                            QString::fromUtf8(error.localizedDescription.UTF8String)});
                    return;
                }
                qCInfo(logNotifications) << "Notification delivered for tier"
                                          << static_cast<int>(tier);
                completeOnMain(std::move(callback),
                    {true, NotificationAuthorizationStatus::Authorized, {}});
            }];
        });
}

bool MacNotificationService::openNotificationSettings(QString& errorMessage)
{
    NSString* urlString = @"x-apple.systempreferences:com.apple.Notifications-Settings.extension";
    NSURL* url = [NSURL URLWithString:urlString];
    if (!url || ![[NSWorkspace sharedWorkspace] openURL:url]) {
        errorMessage = QStringLiteral(
            "Unable to open Notification settings. Open System Settings > Notifications manually.");
        qCWarning(logNotifications) << errorMessage;
        return false;
    }
    return true;
}
