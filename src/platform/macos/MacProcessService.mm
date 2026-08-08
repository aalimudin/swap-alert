#include "platform/macos/MacProcessService.hpp"

#import <AppKit/AppKit.h>
#include <algorithm>
#include <libproc.h>
#include <unistd.h>

QVector<ApplicationProcess> MacProcessService::runningApplications() const
{
    QVector<ApplicationProcess> result;
    const pid_t ownProcessId = getpid();

    for (NSRunningApplication* application in [NSWorkspace sharedWorkspace].runningApplications) {
        if (application.processIdentifier == ownProcessId
            || application.activationPolicy != NSApplicationActivationPolicyRegular
            || application.terminated) {
            continue;
        }

        rusage_info_v4 usage {};
        quint64 footprint = 0;
        if (proc_pid_rusage(application.processIdentifier, RUSAGE_INFO_V4,
                reinterpret_cast<rusage_info_t*>(&usage)) == 0) {
            footprint = usage.ri_phys_footprint;
        }

        const QString name = application.localizedName
            ? QString::fromUtf8(application.localizedName.UTF8String)
            : QStringLiteral("Process %1").arg(application.processIdentifier);
        result.push_back({application.processIdentifier, name, footprint});
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.memoryBytes > right.memoryBytes;
    });
    return result;
}

bool MacProcessService::terminate(qint64 processId, bool force, QString& errorMessage)
{
    NSRunningApplication* application =
        [NSRunningApplication runningApplicationWithProcessIdentifier:static_cast<pid_t>(processId)];
    if (!application || application.terminated) {
        errorMessage = QStringLiteral("The application is no longer running.");
        return false;
    }

    const bool accepted = force ? application.forceTerminate : application.terminate;
    if (!accepted) {
        errorMessage = force
            ? QStringLiteral("macOS rejected the force-quit request.")
            : QStringLiteral("The application did not accept the quit request.");
    }
    return accepted;
}

