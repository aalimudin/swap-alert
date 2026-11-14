#include "platform/linux/LinuxNotificationService.hpp"

#include "core/Logging.hpp"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QProcess>
#include <QStandardPaths>
#include <QVariantMap>

namespace {
constexpr auto notificationService = "org.freedesktop.Notifications";
constexpr auto notificationPath = "/org/freedesktop/Notifications";

void complete(NotificationCallback callback, const NotificationResult& result)
{
    if (callback) {
        callback(result);
    }
}
}

NotificationResult LinuxNotificationService::serviceStatus() const
{
    const auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return {false, NotificationAuthorizationStatus::Unknown,
            QStringLiteral("The desktop session D-Bus is unavailable.")};
    }
    const auto* interface = bus.interface();
    if (!interface) {
        return {false, NotificationAuthorizationStatus::Unknown,
            QStringLiteral("The desktop session D-Bus interface is unavailable.")};
    }
    const QDBusReply<bool> registered = interface->isServiceRegistered(
        QString::fromLatin1(notificationService));
    if (!registered.isValid() || !registered.value()) {
        return {false, NotificationAuthorizationStatus::Unknown,
            QStringLiteral("No freedesktop notification service is running.")};
    }
    return {true, NotificationAuthorizationStatus::Authorized, {}};
}

void LinuxNotificationService::authorizationStatus(NotificationCallback callback)
{
    complete(std::move(callback), serviceStatus());
}

void LinuxNotificationService::requestAuthorization(NotificationCallback callback)
{
    // Freedesktop notification servers do not expose an application permission request.
    complete(std::move(callback), serviceStatus());
}

void LinuxNotificationService::send(const QString& title, const QString& body, AlertTier tier,
    NotificationCallback callback)
{
    const auto status = serviceStatus();
    if (!status.success) {
        qCWarning(logNotifications) << status.message;
        complete(std::move(callback), status);
        return;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("com.swapalert.app"));
    hints.insert(QStringLiteral("urgency"),
        QVariant::fromValue(static_cast<uchar>(tier == AlertTier::Tier3 ? 2 : tier == AlertTier::Tier2 ? 1 : 0)));
    const int timeoutMs = tier == AlertTier::Tier1 ? 10'000 : 0;
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(notificationService),
        QString::fromLatin1(notificationPath), QString::fromLatin1(notificationService),
        QStringLiteral("Notify"));
    message << QStringLiteral("Swap Alert") << uint(0) << QStringLiteral("com.swapalert.app") << title
            << body << QStringList {} << hints << timeoutMs;

    // Asynchronous, so a slow or hung notification daemon can't freeze the tray while we wait.
    auto* watcher = new QDBusPendingCallWatcher(
        QDBusConnection::sessionBus().asyncCall(message, 3000));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
        [tier, callback](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<uint> reply = *call;
            if (reply.isError()) {
                const QString errorMessage = QStringLiteral("Notification delivery failed: %1")
                                                 .arg(reply.error().message());
                qCWarning(logNotifications) << errorMessage;
                complete(callback, {false, NotificationAuthorizationStatus::Authorized, errorMessage});
                return;
            }

            qCInfo(logNotifications) << "Notification delivered for tier" << static_cast<int>(tier)
                                     << "with ID" << reply.value();
            complete(callback, {true, NotificationAuthorizationStatus::Authorized, {}});
        });
}

bool LinuxNotificationService::openNotificationSettings(QString& errorMessage)
{
    struct SettingsApplication {
        const char* executable;
        QStringList arguments;
    };
    const QList<SettingsApplication> candidates {
        {"gnome-control-center", {QStringLiteral("notifications")}},
        {"systemsettings", {QStringLiteral("kcm_notifications")}},
        {"systemsettings6", {QStringLiteral("kcm_notifications")}},
        {"xfce4-notifyd-config", {}},
    };
    for (const auto& candidate : candidates) {
        const QString executable = QStandardPaths::findExecutable(
            QString::fromLatin1(candidate.executable));
        if (!executable.isEmpty() && QProcess::startDetached(executable, candidate.arguments)) {
            return true;
        }
    }

    errorMessage = QStringLiteral(
        "Unable to open notification settings. Open your desktop environment's notification settings manually.");
    return false;
}
