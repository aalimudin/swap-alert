#pragma once

#include <QString>
#include <QVector>

struct ProcessSnapshot {
    qint64 processId = 0;
    qint64 parentProcessId = 0;
    quint32 userId = 0;
    quint64 memoryBytes = 0;
    QString bundleIdentifier;
};

struct ApplicationRoot {
    qint64 processId = 0;
    QString name;
    QString bundleIdentifier;
};

struct ApplicationProcess {
    qint64 processId = 0;
    QString name;
    QString bundleIdentifier;
    quint64 memoryBytes = 0;
    int processCount = 0;
};

[[nodiscard]] QVector<ApplicationProcess> groupApplicationProcesses(
    const QVector<ApplicationRoot>& roots, const QVector<ProcessSnapshot>& snapshots,
    quint32 currentUserId);

