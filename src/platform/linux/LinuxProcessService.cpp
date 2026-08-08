#include "platform/linux/LinuxProcessService.hpp"

#include "core/Logging.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <optional>
#include <unistd.h>

namespace {
struct DesktopApplication {
    QString identifier;
    QString name;
    QString executableName;
};

struct LinuxProcess {
    ProcessSnapshot snapshot;
    QString executableName;
    QString desktopIdentifier;
};

QString desktopIdentifierFromPath(const QString& path)
{
    return QFileInfo(path).fileName().remove(QStringLiteral(".desktop"));
}

QVector<DesktopApplication> desktopApplications()
{
    QHash<QString, DesktopApplication> byIdentifier;
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    for (const QString& location : locations) {
        QDir directory(location);
        const auto files = directory.entryInfoList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QFileInfo& info : files) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            QString name;
            QString exec;
            bool inDesktopEntry = false;
            bool hidden = false;
            bool noDisplay = false;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inDesktopEntry = line == QStringLiteral("[Desktop Entry]");
                    continue;
                }
                if (!inDesktopEntry || line.startsWith(QLatin1Char('#'))) {
                    continue;
                }
                if (line.startsWith(QStringLiteral("Name=")) && name.isEmpty()) {
                    name = line.mid(5).trimmed();
                } else if (line.startsWith(QStringLiteral("Exec="))) {
                    exec = line.mid(5).trimmed();
                } else if (line == QStringLiteral("Hidden=true")) {
                    hidden = true;
                } else if (line == QStringLiteral("NoDisplay=true")) {
                    noDisplay = true;
                }
            }
            if (hidden || noDisplay || name.isEmpty() || exec.isEmpty()) {
                continue;
            }
            const QStringList command = QProcess::splitCommand(exec);
            if (command.isEmpty()) {
                continue;
            }
            QString executable = QFileInfo(command.first()).fileName();
            if (executable == QStringLiteral("env") && command.size() > 1) {
                for (qsizetype index = 1; index < command.size(); ++index) {
                    if (!command[index].contains(QLatin1Char('='))) {
                        executable = QFileInfo(command[index]).fileName();
                        break;
                    }
                }
            }
            if (executable == QStringLiteral("flatpak") || executable == QStringLiteral("snap")) {
                continue;
            }
            const QString identifier = desktopIdentifierFromPath(info.fileName());
            if (!byIdentifier.contains(identifier)) {
                byIdentifier.insert(identifier, {identifier, name, executable});
            }
        }
    }
    return byIdentifier.values();
}

QHash<QString, QString> readEnvironment(qint64 processId)
{
    QFile file(QStringLiteral("/proc/%1/environ").arg(processId));
    QHash<QString, QString> result;
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const auto entries = file.readAll().split('\0');
    for (const QByteArray& entry : entries) {
        const qsizetype separator = entry.indexOf('=');
        if (separator > 0) {
            result.insert(QString::fromUtf8(entry.first(separator)),
                QString::fromUtf8(entry.sliced(separator + 1)));
        }
    }
    return result;
}

std::optional<LinuxProcess> readProcess(qint64 processId, quint32 requiredUserId)
{
    QFile status(QStringLiteral("/proc/%1/status").arg(processId));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::nullopt;
    }

    std::optional<quint32> userId;
    qint64 parentId = 0;
    quint64 memoryBytes = 0;
    while (!status.atEnd()) {
        const QByteArray line = status.readLine();
        if (line.startsWith("Uid:")) {
            const auto fields = line.simplified().split(' ');
            bool valid = false;
            const uint value = fields.value(1).toUInt(&valid);
            if (valid) {
                userId = value;
            }
        } else if (line.startsWith("PPid:")) {
            bool valid = false;
            const qint64 value = line.sliced(5).trimmed().toLongLong(&valid);
            if (valid) {
                parentId = value;
            }
        } else if (line.startsWith("VmRSS:")) {
            const auto fields = line.simplified().split(' ');
            bool valid = false;
            const quint64 kibibytes = fields.value(1).toULongLong(&valid);
            if (valid) {
                memoryBytes = kibibytes * 1024;
            }
        }
    }
    if (!userId || *userId != requiredUserId) {
        return std::nullopt;
    }

    const QString executablePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(processId)).symLinkTarget();
    if (executablePath.isEmpty()) {
        return std::nullopt;
    }
    const auto environment = readEnvironment(processId);
    QString desktopIdentifier;
    if (environment.contains(QStringLiteral("GIO_LAUNCHED_DESKTOP_FILE"))) {
        desktopIdentifier = desktopIdentifierFromPath(
            environment.value(QStringLiteral("GIO_LAUNCHED_DESKTOP_FILE")));
    } else if (environment.contains(QStringLiteral("FLATPAK_ID"))) {
        desktopIdentifier = environment.value(QStringLiteral("FLATPAK_ID"));
    } else if (environment.contains(QStringLiteral("SNAP_INSTANCE_NAME"))) {
        desktopIdentifier = environment.value(QStringLiteral("SNAP_INSTANCE_NAME"));
    }

    return LinuxProcess {{processId, parentId, *userId, memoryBytes, desktopIdentifier},
        QFileInfo(executablePath).fileName(), desktopIdentifier};
}

bool isProtected(const LinuxProcess& process)
{
    static const QSet<QString> protectedExecutables {
        QStringLiteral("Swap Alert"), QStringLiteral("swap-alert"),
        QStringLiteral("systemd"), QStringLiteral("dbus-daemon"),
        QStringLiteral("gnome-shell"), QStringLiteral("gnome-session-binary"),
        QStringLiteral("plasmashell"), QStringLiteral("kwin_wayland"),
        QStringLiteral("kwin_x11"), QStringLiteral("xfce4-panel"),
        QStringLiteral("xfdesktop"), QStringLiteral("Xorg"),
        QStringLiteral("Xwayland"), QStringLiteral("sddm"),
        QStringLiteral("gdm-session-worker"), QStringLiteral("xdg-desktop-portal"),
        QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("zsh"),
        QStringLiteral("python"), QStringLiteral("python3"), QStringLiteral("java"),
        QStringLiteral("wine"), QStringLiteral("flatpak"), QStringLiteral("snap"),
    };
    static const QSet<QString> protectedIdentifiers {
        QStringLiteral("org.gnome.Shell"), QStringLiteral("org.kde.plasmashell"),
        QStringLiteral("org.xfce.xfce4-panel"), QStringLiteral("com.swapalert.app"),
    };
    return protectedExecutables.contains(process.executableName)
        || protectedIdentifiers.contains(process.desktopIdentifier);
}

struct ProcessInventory {
    QVector<LinuxProcess> processes;
    QVector<ApplicationRoot> roots;
};

ProcessInventory inventory()
{
    const quint32 currentUserId = getuid();
    const qint64 ownProcessId = getpid();
    const auto applications = desktopApplications();
    QHash<QString, DesktopApplication> byIdentifier;
    QMultiHash<QString, DesktopApplication> byExecutable;
    for (const auto& application : applications) {
        byIdentifier.insert(application.identifier, application);
        byExecutable.insert(application.executableName, application);
    }

    QVector<LinuxProcess> processes;
    QDir proc(QStringLiteral("/proc"));
    const auto entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    processes.reserve(entries.size());
    for (const QString& entry : entries) {
        bool valid = false;
        const qint64 processId = entry.toLongLong(&valid);
        if (!valid || processId <= 1 || processId == ownProcessId) {
            continue;
        }
        auto process = readProcess(processId, currentUserId);
        if (!process || isProtected(*process)) {
            continue;
        }
        if (process->desktopIdentifier.isEmpty()) {
            const auto matches = byExecutable.values(process->executableName);
            if (matches.size() == 1) {
                process->desktopIdentifier = matches.first().identifier;
                process->snapshot.bundleIdentifier = process->desktopIdentifier;
            }
        }
        processes.push_back(std::move(*process));
    }

    QHash<qint64, QString> identityByProcess;
    for (const auto& process : processes) {
        identityByProcess.insert(process.snapshot.processId, process.desktopIdentifier);
    }
    QHash<QString, LinuxProcess> rootByIdentifier;
    for (const auto& process : processes) {
        if (process.desktopIdentifier.isEmpty() || !byIdentifier.contains(process.desktopIdentifier)) {
            continue;
        }
        const bool parentHasSameIdentity = identityByProcess.value(process.snapshot.parentProcessId)
            == process.desktopIdentifier;
        if (parentHasSameIdentity) {
            continue;
        }
        const auto existing = rootByIdentifier.constFind(process.desktopIdentifier);
        if (existing == rootByIdentifier.cend()
            || process.snapshot.processId < existing->snapshot.processId) {
            rootByIdentifier.insert(process.desktopIdentifier, process);
        }
    }

    QVector<ApplicationRoot> roots;
    roots.reserve(rootByIdentifier.size());
    for (const auto& process : rootByIdentifier) {
        const auto application = byIdentifier.value(process.desktopIdentifier);
        roots.push_back({process.snapshot.processId, application.name, application.identifier});
    }
    return {std::move(processes), std::move(roots)};
}
}

QVector<ApplicationProcess> LinuxProcessService::runningApplications() const
{
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        qCInfo(logProcesses) << "Host process cleanup is unavailable in the Flatpak sandbox";
        return {};
    }
    auto available = inventory();
    QVector<ProcessSnapshot> snapshots;
    snapshots.reserve(available.processes.size());
    for (const auto& process : available.processes) {
        snapshots.push_back(process.snapshot);
    }
    auto applications = groupApplicationProcesses(available.roots, snapshots, getuid());
    qCInfo(logProcesses) << "Listed" << applications.size()
                         << "eligible Linux applications from" << snapshots.size() << "processes";
    return applications;
}

bool LinuxProcessService::terminate(qint64 processId, bool force, QString& errorMessage)
{
    if (qEnvironmentVariableIsSet("FLATPAK_ID")) {
        errorMessage = QStringLiteral(
            "Host application cleanup is unavailable in the Flatpak sandbox.");
        return false;
    }
    qCInfo(logProcesses) << (force ? "Force-quit" : "Quit") << "requested for PID"
                         << processId;
    if (processId <= 1 || processId == getpid()) {
        errorMessage = QStringLiteral("Swap Alert will not terminate this protected process.");
        return false;
    }

    const auto process = readProcess(processId, getuid());
    if (!process) {
        errorMessage = QStringLiteral("The process is no longer running or its owner could not be verified.");
        return false;
    }
    if (isProtected(*process)) {
        errorMessage = QStringLiteral("Swap Alert will not terminate protected desktop processes.");
        return false;
    }

    const auto available = inventory();
    const bool eligible = std::any_of(available.roots.cbegin(), available.roots.cend(),
        [processId](const ApplicationRoot& root) { return root.processId == processId; });
    if (!eligible) {
        errorMessage = QStringLiteral("The process is not an eligible current-user application.");
        return false;
    }

    if (::kill(static_cast<pid_t>(processId), force ? SIGKILL : SIGTERM) != 0) {
        errorMessage = QStringLiteral("Unable to signal the application: %1")
                           .arg(QString::fromLocal8Bit(std::strerror(errno)));
        qCWarning(logProcesses) << errorMessage << "PID" << processId;
        return false;
    }
    qCInfo(logProcesses) << "Signal accepted for PID" << processId;
    return true;
}
