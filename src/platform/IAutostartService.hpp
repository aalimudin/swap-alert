#pragma once

#include <QString>

class IAutostartService {
public:
    virtual ~IAutostartService() = default;
    [[nodiscard]] virtual bool isEnabled() const = 0;
    virtual bool setEnabled(bool enabled, QString& errorMessage) = 0;
};

