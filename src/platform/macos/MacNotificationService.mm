#include "platform/macos/MacNotificationService.hpp"

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
}

void MacNotificationService::requestAuthorization()
{
    UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
    center.delegate = notificationDelegate();
    [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                          completionHandler:^(BOOL, NSError*) {
                          }];
}

void MacNotificationService::send(const QString& title, const QString& body, AlertTier tier)
{
    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = toNSString(title);
    content.body = toNSString(body);
    content.sound = tier == AlertTier::Tier1 ? nil : [UNNotificationSound defaultSound];

    NSString* identifier = [[NSUUID UUID] UUIDString];
    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:identifier
                                                                          content:content
                                                                          trigger:nil];
    [[UNUserNotificationCenter currentNotificationCenter]
        addNotificationRequest:request
         withCompletionHandler:^(NSError*) {
         }];
}
