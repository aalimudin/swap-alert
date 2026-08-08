#include "platform/linux/LinuxAutostartService.hpp"

#include "core/Logging.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace {
QString desktopExecQuote(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    value.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    value.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

struct AutostartCommand {
    QString exec;
    QString tryExec;
};

AutostartCommand autostartCommand()
{
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    if (!flatpakId.isEmpty()) {
        return {QStringLiteral("flatpak run ") + desktopExecQuote(flatpakId),
            QStringLiteral("flatpak")};
    }
    const QString executable = QCoreApplication::applicationFilePath();
    return {desktopExecQuote(executable), executable};
}
}

QString LinuxAutostartService::desktopFilePath() const
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(config).filePath(QStringLiteral("autostart/com.swapalert.app.desktop"));
}

bool LinuxAutostartService::isEnabled() const
{
    QFile file(desktopFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents = file.readAll();
    return !contents.contains("Hidden=true") && contents.contains("X-SwapAlert-Autostart=true");
}

bool LinuxAutostartService::setEnabled(bool enabled, QString& errorMessage)
{
    const QString path = desktopFilePath();
    if (!enabled) {
        if (!QFileInfo::exists(path) || QFile::remove(path)) {
            qCInfo(logSystem) << "Start at login disabled";
            return true;
        }
        errorMessage = QStringLiteral("Unable to remove %1.").arg(path);
        qCWarning(logSystem) << errorMessage;
        return false;
    }

    const QString executable = QCoreApplication::applicationFilePath();
    if (executable.isEmpty() || !QFileInfo(executable).isExecutable()) {
        errorMessage = QStringLiteral("The Swap Alert executable path is unavailable.");
        return false;
    }

    const QFileInfo destination(path);
    if (!QDir().mkpath(destination.absolutePath())) {
        errorMessage = QStringLiteral("Unable to create %1.").arg(destination.absolutePath());
        return false;
    }

    const auto command = autostartCommand();
    const QByteArray desktopEntry = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Version=1.0\n"
        "Name=Swap Alert\n"
        "Comment=Monitor system swap usage\n"
        "Exec=%1\n"
        "TryExec=%2\n"
        "Icon=com.swapalert.app\n"
        "Terminal=false\n"
        "Categories=Utility;System;\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-SwapAlert-Autostart=true\n")
                                      .arg(command.exec, command.tryExec)
                                      .toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(desktopEntry) != desktopEntry.size() || !file.commit()) {
        errorMessage = QStringLiteral("Unable to write %1: %2").arg(path, file.errorString());
        qCWarning(logSystem) << errorMessage;
        return false;
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qCInfo(logSystem) << "Start at login enabled using" << path;
    return true;
}
