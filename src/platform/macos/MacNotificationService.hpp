#pragma once

#include "platform/INotificationService.hpp"

class MacNotificationService final : public INotificationService {
public:
    void requestAuthorization() override;
    void send(const QString& title, const QString& body, AlertTier tier) override;
};

