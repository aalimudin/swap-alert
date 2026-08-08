#pragma once

#include "core/AlertTier.hpp"

#include <QString>

class INotificationService {
public:
    virtual ~INotificationService() = default;
    virtual void requestAuthorization() = 0;
    virtual void send(const QString& title, const QString& body, AlertTier tier) = 0;
};

