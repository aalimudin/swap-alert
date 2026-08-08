#include "core/ProcessGrouping.hpp"

#include <QHash>
#include <QMultiHash>
#include <QQueue>
#include <QSet>
#include <algorithm>

namespace {
bool belongsToBundle(const QString& candidate, const QString& root)
{
    return !root.isEmpty()
        && (candidate == root || candidate.startsWith(root + QLatin1Char('.')));
}
}

QVector<ApplicationProcess> groupApplicationProcesses(const QVector<ApplicationRoot>& roots,
    const QVector<ProcessSnapshot>& snapshots, quint32 currentUserId)
{
    QHash<qint64, ProcessSnapshot> byProcessId;
    QMultiHash<qint64, qint64> childrenByParent;
    for (const auto& snapshot : snapshots) {
        if (snapshot.processId <= 0 || snapshot.userId != currentUserId) {
            continue;
        }
        byProcessId.insert(snapshot.processId, snapshot);
        childrenByParent.insert(snapshot.parentProcessId, snapshot.processId);
    }

    QSet<qint64> rootProcessIds;
    for (const auto& root : roots) {
        rootProcessIds.insert(root.processId);
    }

    QVector<ApplicationProcess> result;
    result.reserve(roots.size());
    for (const auto& root : roots) {
        QQueue<qint64> pending;
        QSet<qint64> included;
        pending.enqueue(root.processId);

        for (const auto& snapshot : snapshots) {
            if (snapshot.userId == currentUserId
                && belongsToBundle(snapshot.bundleIdentifier, root.bundleIdentifier)) {
                pending.enqueue(snapshot.processId);
            }
        }

        quint64 memoryBytes = 0;
        while (!pending.isEmpty()) {
            const qint64 processId = pending.dequeue();
            if (included.contains(processId)
                || (processId != root.processId && rootProcessIds.contains(processId))) {
                continue;
            }

            const auto snapshot = byProcessId.constFind(processId);
            if (snapshot == byProcessId.cend()) {
                continue;
            }

            included.insert(processId);
            memoryBytes += snapshot->memoryBytes;
            const auto children = childrenByParent.values(processId);
            for (qint64 childProcessId : children) {
                pending.enqueue(childProcessId);
            }
        }

        result.push_back({root.processId, root.name, root.bundleIdentifier, memoryBytes,
            static_cast<int>(included.size())});
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.memoryBytes != right.memoryBytes) {
            return left.memoryBytes > right.memoryBytes;
        }
        return left.name.localeAwareCompare(right.name) < 0;
    });
    return result;
}
