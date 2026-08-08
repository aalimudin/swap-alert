#pragma once

#include "core/AlertTier.hpp"

#include <QString>
#include <functional>

enum class NotificationAuthorizationStatus {
    Unknown,
    NotDetermined,
    Denied,
    Authorized,
};

struct NotificationResult {
    bool success = false;
    NotificationAuthorizationStatus authorization = NotificationAuthorizationStatus::Unknown;
    QString message;
};

using NotificationCallback = std::function<void(const NotificationResult&)>;

class INotificationService {
public:
    virtual ~INotificationService() = default;
    virtual void authorizationStatus(NotificationCallback callback) = 0;
    virtual void requestAuthorization(NotificationCallback callback) = 0;
    virtual void send(const QString& title, const QString& body, AlertTier tier,
        NotificationCallback callback) = 0;
    virtual bool openNotificationSettings(QString& errorMessage) = 0;
};
