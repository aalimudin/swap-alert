#pragma once

#include <QString>
#include <QVector>

struct ApplicationProcess {
    qint64 processId = 0;
    QString name;
    quint64 memoryBytes = 0;
};

class IProcessService {
public:
    virtual ~IProcessService() = default;
    [[nodiscard]] virtual QVector<ApplicationProcess> runningApplications() const = 0;
    virtual bool terminate(qint64 processId, bool force, QString& errorMessage) = 0;
};

