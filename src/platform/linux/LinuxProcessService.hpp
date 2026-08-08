#pragma once

#include "platform/IProcessService.hpp"

class LinuxProcessService final : public IProcessService {
public:
    [[nodiscard]] QVector<ApplicationProcess> runningApplications() const override;
    bool terminate(qint64 processId, bool force, QString& errorMessage) override;
};
