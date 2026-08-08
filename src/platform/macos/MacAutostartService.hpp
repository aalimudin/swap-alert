#pragma once

#include "platform/IAutostartService.hpp"

class MacAutostartService final : public IAutostartService {
public:
    [[nodiscard]] bool isEnabled() const override;
    bool setEnabled(bool enabled, QString& errorMessage) override;
};

