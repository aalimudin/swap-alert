#include "platform/linux/LinuxSystemEventMonitor.hpp"

#include "core/Logging.hpp"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QVariantMap>
#include <utility>
#include <unistd.h>

class LinuxSystemEventMonitorImpl final : public QObject {
    Q_OBJECT

public:
    LinuxSystemEventMonitorImpl()
        : watcher(QString(), QDBusConnection::sessionBus(),
              QDBusServiceWatcher::WatchForRegistration, this)
    {
        connect(&watcher, &QDBusServiceWatcher::serviceRegistered, this,
            [this](const QString&) { emitEvent(SystemEvent::DesktopRestored); });
    }

    void start(SystemEventCallback newCallback)
    {
        stop();
        callback = std::move(newCallback);
        auto systemBus = QDBusConnection::systemBus();
        if (systemBus.isConnected()) {
            systemBus.connect(QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"),
                QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));
            systemBus.connect(QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"),
                QStringLiteral("PrepareForShutdown"), this, SLOT(prepareForShutdown(bool)));

            QDBusInterface manager(QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"), systemBus);
            manager.setTimeout(3000);
            const QDBusReply<QDBusObjectPath> session = manager.call(
                QStringLiteral("GetSessionByPID"), static_cast<uint>(getpid()));
            if (session.isValid()) {
                sessionPath = session.value().path();
                systemBus.connect(QStringLiteral("org.freedesktop.login1"), sessionPath,
                    QStringLiteral("org.freedesktop.DBus.Properties"),
                    QStringLiteral("PropertiesChanged"), this,
                    SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
            } else {
                qCWarning(logSystem) << "Unable to monitor login session:"
                                     << session.error().message();
            }
        } else {
            qCWarning(logSystem) << "System D-Bus is unavailable; sleep events will not be monitored";
        }

        watcher.addWatchedService(QStringLiteral("org.freedesktop.Notifications"));
        watcher.addWatchedService(QStringLiteral("org.kde.StatusNotifierWatcher"));
    }

    void stop()
    {
        auto systemBus = QDBusConnection::systemBus();
        systemBus.disconnect(QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
            this, SLOT(prepareForSleep(bool)));
        systemBus.disconnect(QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForShutdown"), this, SLOT(prepareForShutdown(bool)));
        if (!sessionPath.isEmpty()) {
            systemBus.disconnect(QStringLiteral("org.freedesktop.login1"), sessionPath,
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"), this,
                SLOT(propertiesChanged(QString,QVariantMap,QStringList)));
        }
        watcher.removeWatchedService(QStringLiteral("org.freedesktop.Notifications"));
        watcher.removeWatchedService(QStringLiteral("org.kde.StatusNotifierWatcher"));
        sessionPath.clear();
        callback = {};
    }

private slots:
    void prepareForSleep(bool sleeping)
    {
        emitEvent(sleeping ? SystemEvent::WillSleep : SystemEvent::DidWake);
    }

    void prepareForShutdown(bool shuttingDown)
    {
        if (shuttingDown) {
            emitEvent(SystemEvent::WillPowerOff);
        }
    }

    void propertiesChanged(const QString& interface, const QVariantMap& changed,
        const QStringList&)
    {
        if (interface != QStringLiteral("org.freedesktop.login1.Session")
            || !changed.contains(QStringLiteral("Active"))) {
            return;
        }
        emitEvent(changed.value(QStringLiteral("Active")).toBool()
                ? SystemEvent::SessionActive
                : SystemEvent::SessionInactive);
    }

private:
    void emitEvent(SystemEvent event)
    {
        if (callback) {
            callback(event);
        }
    }

    SystemEventCallback callback;
    QString sessionPath;
    QDBusServiceWatcher watcher;
};

LinuxSystemEventMonitor::LinuxSystemEventMonitor()
    : m_impl(std::make_unique<LinuxSystemEventMonitorImpl>())
{
}

LinuxSystemEventMonitor::~LinuxSystemEventMonitor()
{
    stop();
}

void LinuxSystemEventMonitor::start(SystemEventCallback callback)
{
    m_impl->start(std::move(callback));
}

void LinuxSystemEventMonitor::stop()
{
    m_impl->stop();
}

#include "LinuxSystemEventMonitor.moc"
