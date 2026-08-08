#pragma once

#include "platform/INotificationService.hpp"

class MacNotificationService final : public INotificationService {
public:
    void authorizationStatus(NotificationCallback callback) override;
    void requestAuthorization(NotificationCallback callback) override;
    void send(const QString& title, const QString& body, AlertTier tier,
        NotificationCallback callback) override;
    bool openNotificationSettings(QString& errorMessage) override;
};
