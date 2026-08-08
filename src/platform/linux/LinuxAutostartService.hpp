#pragma once

#include "platform/IAutostartService.hpp"

class LinuxAutostartService final : public IAutostartService {
public:
    [[nodiscard]] bool isEnabled() const override;
    bool setEnabled(bool enabled, QString& errorMessage) override;

private:
    [[nodiscard]] QString desktopFilePath() const;
};
