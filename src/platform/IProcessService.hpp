#pragma once

#include "core/ProcessGrouping.hpp"

#include <QString>
#include <QVector>

class IProcessService {
public:
    virtual ~IProcessService() = default;
    [[nodiscard]] virtual QVector<ApplicationProcess> runningApplications() const = 0;
    virtual bool terminate(qint64 processId, bool force, QString& errorMessage) = 0;
};
