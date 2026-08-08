#include "platform/macos/MacProcessService.hpp"

#include "core/Logging.hpp"

#import <AppKit/AppKit.h>
#include <QHash>
#include <QSet>
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>

namespace {
QString fromNSString(NSString* value)
{
    return value ? QString::fromUtf8(value.UTF8String) : QString();
}

quint64 physicalFootprint(pid_t processId)
{
    rusage_info_v4 usage {};
    if (proc_pid_rusage(processId, RUSAGE_INFO_V4,
            reinterpret_cast<rusage_info_t*>(&usage)) != 0) {
        return 0;
    }
    return usage.ri_phys_footprint;
}

bool isProtectedApplication(NSRunningApplication* application)
{
    if (!application) {
        return true;
    }

    static NSSet<NSString*>* protectedIdentifiers = [NSSet setWithArray:@[
        @"com.apple.finder",
        @"com.apple.dock",
        @"com.apple.loginwindow",
        @"com.apple.SystemUIServer",
        @"com.apple.WindowManager"
    ]];

    NSString* bundleIdentifier = application.bundleIdentifier;
    if (bundleIdentifier && [protectedIdentifiers containsObject:bundleIdentifier]) {
        return true;
    }

    NSString* bundlePath = application.bundleURL.path;
    return bundlePath && [bundlePath hasPrefix:@"/System/Library/CoreServices/"];
}

bool readBsdInfo(pid_t processId, proc_bsdinfo& info)
{
    return proc_pidinfo(processId, PROC_PIDTBSDINFO, 0, &info, sizeof(info))
        == static_cast<int>(sizeof(info));
}
}

QVector<ApplicationProcess> MacProcessService::runningApplications() const
{
    const pid_t ownProcessId = getpid();
    const uid_t currentUserId = getuid();
    NSArray<NSRunningApplication*>* running = [NSWorkspace sharedWorkspace].runningApplications;

    QHash<qint64, QString> bundleIdentifierByProcess;
    QVector<ApplicationRoot> roots;
    for (NSRunningApplication* application in running) {
        const pid_t processId = application.processIdentifier;
        bundleIdentifierByProcess.insert(processId, fromNSString(application.bundleIdentifier));

        if (processId == ownProcessId
            || application.activationPolicy != NSApplicationActivationPolicyRegular
            || application.terminated || isProtectedApplication(application)) {
            continue;
        }

        proc_bsdinfo bsdInfo {};
        if (!readBsdInfo(processId, bsdInfo) || bsdInfo.pbi_uid != currentUserId) {
            continue;
        }

        const QString name = application.localizedName
            ? fromNSString(application.localizedName)
            : QStringLiteral("Process %1").arg(processId);
        roots.push_back({processId, name, fromNSString(application.bundleIdentifier)});
    }

    QVector<ProcessSnapshot> snapshots;
    const int capacity = proc_listallpids(nullptr, 0);
    if (capacity > 0) {
        QVector<pid_t> processIds(capacity + 32);
        const int listed = proc_listallpids(
            processIds.data(), static_cast<int>(processIds.size() * sizeof(pid_t)));
        if (listed > 0) {
            processIds.resize(qMin(listed, processIds.size()));
            snapshots.reserve(processIds.size());
            for (pid_t processId : processIds) {
                proc_bsdinfo bsdInfo {};
                if (processId <= 0 || !readBsdInfo(processId, bsdInfo)
                    || bsdInfo.pbi_uid != currentUserId) {
                    continue;
                }
                snapshots.push_back({processId, bsdInfo.pbi_ppid, bsdInfo.pbi_uid,
                    physicalFootprint(processId), bundleIdentifierByProcess.value(processId)});
            }
        }
    }

    QSet<qint64> capturedProcessIds;
    for (const auto& snapshot : snapshots) {
        capturedProcessIds.insert(snapshot.processId);
    }
    for (const auto& root : roots) {
        if (!capturedProcessIds.contains(root.processId)) {
            snapshots.push_back({root.processId, 0, static_cast<quint32>(currentUserId),
                physicalFootprint(static_cast<pid_t>(root.processId)), root.bundleIdentifier});
        }
    }

    auto applications = groupApplicationProcesses(roots, snapshots, currentUserId);
    qCInfo(logProcesses) << "Listed" << applications.size()
                         << "eligible applications from" << snapshots.size() << "processes";
    return applications;
}

bool MacProcessService::terminate(qint64 processId, bool force, QString& errorMessage)
{
    qCInfo(logProcesses) << (force ? "Force-quit" : "Quit") << "requested for PID"
                         << processId;
    if (processId <= 1 || processId == getpid()) {
        errorMessage = QStringLiteral("Swap Alert will not terminate this protected process.");
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
        return false;
    }

    NSRunningApplication* application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(processId)];
    if (!application || application.terminated) {
        errorMessage = QStringLiteral("The application is no longer running.");
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
        return false;
    }

    proc_bsdinfo bsdInfo {};
    if (!readBsdInfo(static_cast<pid_t>(processId), bsdInfo)
        || bsdInfo.pbi_uid != getuid()) {
        errorMessage = QStringLiteral("The application owner could not be verified.");
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
        return false;
    }

    if (application.activationPolicy != NSApplicationActivationPolicyRegular
        || isProtectedApplication(application)) {
        errorMessage = QStringLiteral("Swap Alert will not terminate protected system applications.");
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
        return false;
    }

    const bool accepted = force ? application.forceTerminate : application.terminate;
    if (!accepted) {
        errorMessage = force
            ? QStringLiteral("macOS rejected the force-quit request.")
            : QStringLiteral("The application did not accept the quit request.");
    }
    if (accepted) {
        qCInfo(logProcesses) << "macOS accepted the termination request for PID" << processId;
    } else {
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
    }
    return accepted;
}
