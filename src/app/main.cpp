#include "core/DiagnosticLogger.hpp"
#include "core/Logging.hpp"
#include "core/SettingsStore.hpp"
#include "core/SwapMonitor.hpp"
#include "platform/macos/MacAutostartService.hpp"
#include "platform/macos/MacContinuousClock.hpp"
#include "platform/macos/MacNotificationService.hpp"
#include "platform/macos/MacProcessService.hpp"
#include "platform/macos/MacSwapReader.hpp"
#include "platform/macos/MacSystemEventMonitor.hpp"
#include "ui/CleanupDialog.hpp"
#include "ui/Format.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/StatusDialog.hpp"
#include "ui/TrayController.hpp"
#include "ui/WarningDialog.hpp"

#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QUrl>
#include <memory>

namespace {
QString notificationTitle(AlertTier tier)
{
    switch (tier) {
    case AlertTier::Tier3:
        return QStringLiteral("Critical swap usage");
    case AlertTier::Tier2:
        return QStringLiteral("High swap usage");
    case AlertTier::Tier1:
        return QStringLiteral("Swap usage warning");
    case AlertTier::Normal:
    default:
        return QStringLiteral("Swap Alert");
    }
}

QString systemEventName(SystemEvent event)
{
    switch (event) {
    case SystemEvent::WillSleep:
        return QStringLiteral("will-sleep");
    case SystemEvent::DidWake:
        return QStringLiteral("did-wake");
    case SystemEvent::SessionInactive:
        return QStringLiteral("session-inactive");
    case SystemEvent::SessionActive:
        return QStringLiteral("session-active");
    case SystemEvent::DesktopRestored:
        return QStringLiteral("desktop-restored");
    case SystemEvent::WillPowerOff:
        return QStringLiteral("will-power-off");
    }
    return QStringLiteral("unknown");
}
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Swap Alert"));
    QApplication::setOrganizationName(QStringLiteral("SwapAlert"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    if (!DiagnosticLogger::install()) {
        qWarning() << "Unable to initialize the persistent diagnostic log";
    }
    qCInfo(logApp) << "Swap Alert" << QApplication::applicationVersion() << "starting";

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("Swap Alert"),
            QStringLiteral("The macOS menu bar is unavailable."));
        return 1;
    }

    SettingsStore settings;
    MacAutostartService autostartService;
    MacNotificationService notificationService;
    MacProcessService processService;
    MacSystemEventMonitor systemEventMonitor;
    SwapMonitor monitor(std::make_unique<MacSwapReader>(), settings,
        std::make_unique<MacContinuousClock>());
    TrayController tray(settings);
    SettingsDialog settingsDialog(settings, autostartService, notificationService);
    StatusDialog statusDialog(settings);
    WarningDialog warningDialog;
    CleanupDialog cleanupDialog(processService);

    const auto showSettings = [&] {
        settingsDialog.reload();
        settingsDialog.show();
        settingsDialog.raise();
        settingsDialog.activateWindow();
    };
    const auto showDashboard = [&] {
        statusDialog.show();
        statusDialog.raise();
        statusDialog.activateWindow();
    };
    const auto showCleanup = [&] {
        cleanupDialog.refresh();
        cleanupDialog.show();
        cleanupDialog.raise();
        cleanupDialog.activateWindow();
    };
    const auto presentAlert = [&](AlertTier tier, const SwapInfo& info, const QString& body) {
        notificationService.send(notificationTitle(tier), body, tier,
            [&, tier, info](const NotificationResult& result) {
                if (!result.success && (tier == AlertTier::Tier1 || tier == AlertTier::Tier2)) {
                    warningDialog.showWarning(tier, info,
                        result.message.isEmpty()
                            ? QStringLiteral("The macOS notification could not be delivered.")
                            : result.message);
                }
            });

        if (tier == AlertTier::Tier2) {
            warningDialog.showWarning(tier, info);
        } else if (tier == AlertTier::Tier3) {
            showCleanup();
            QApplication::alert(&cleanupDialog);
        }
    };

    QObject::connect(&tray, &TrayController::dashboardRequested, &statusDialog, showDashboard);
    QObject::connect(&tray, &TrayController::settingsRequested, &settingsDialog, showSettings);
    QObject::connect(&tray, &TrayController::logsRequested, &application, [] {
        const QString directory = QFileInfo(DiagnosticLogger::logFilePath()).absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
            qCWarning(logApp) << "Unable to open log directory" << directory;
        }
    });
    QObject::connect(&tray, &TrayController::reviewRequested, &cleanupDialog, showCleanup);
    QObject::connect(&tray, &TrayController::refreshRequested, &monitor,
        &SwapMonitor::refreshNow);
    QObject::connect(&tray, &TrayController::snoozeRequested, &monitor,
        &SwapMonitor::snoozeForMinutes);
    QObject::connect(&tray, &TrayController::snoozeUntilRestartRequested, &monitor,
        &SwapMonitor::snoozeUntilRestart);
    QObject::connect(&tray, &TrayController::monitoringToggled, &settings,
        &SettingsStore::setMonitoringEnabled);
    QObject::connect(&tray, &TrayController::quitRequested, &application,
        &QApplication::quit);

    QObject::connect(&monitor, &SwapMonitor::sampleUpdated, &tray,
        &TrayController::updateSample);
    QObject::connect(&monitor, &SwapMonitor::sampleUpdated, &statusDialog,
        &StatusDialog::updateSample);
    QObject::connect(&monitor, &SwapMonitor::readFailed, &tray,
        &TrayController::showReadError);
    QObject::connect(&monitor, &SwapMonitor::readFailed, &statusDialog,
        &StatusDialog::showReadError);
    QObject::connect(&monitor, &SwapMonitor::alertTriggered, &application,
        [&](AlertTier tier, const SwapInfo& info) {
            const QString body = QStringLiteral("Swap usage has reached %1 of %2.")
                                     .arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes));
            presentAlert(tier, info, body);
        });

    QObject::connect(&warningDialog, &WarningDialog::reviewRequested, &cleanupDialog,
        showCleanup);
    QObject::connect(&warningDialog, &WarningDialog::snoozeRequested, &monitor,
        &SwapMonitor::snoozeForMinutes);

    QObject::connect(&statusDialog, &StatusDialog::refreshRequested, &monitor,
        &SwapMonitor::refreshNow);
    QObject::connect(&statusDialog, &StatusDialog::settingsRequested, &settingsDialog,
        showSettings);
    QObject::connect(&statusDialog, &StatusDialog::reviewRequested, &cleanupDialog,
        showCleanup);

    QObject::connect(&settingsDialog, &SettingsDialog::testAlertRequested, &application,
        [&](int requestedTier) {
            const auto tier = static_cast<AlertTier>(requestedTier);
            SwapInfo example {16ULL * 1024 * 1024 * 1024,
                static_cast<quint64>(requestedTier * 3ULL) * 1024 * 1024 * 1024,
                0};
            presentAlert(tier, example,
                QStringLiteral("This is a Tier %1 test alert.").arg(requestedTier));
        });

    notificationService.requestAuthorization([](const NotificationResult& result) {
        if (!result.success && result.authorization == NotificationAuthorizationStatus::Unknown) {
            qCWarning(logNotifications)
                << "Unable to determine notification authorization:" << result.message;
        }
    });

    bool systemSleeping = false;
    bool sessionActive = true;
    systemEventMonitor.start([&](SystemEvent event) {
        qCInfo(logSystem) << "Received system event" << systemEventName(event);
        switch (event) {
        case SystemEvent::WillSleep:
            systemSleeping = true;
            monitor.suspendForSystemEvent();
            break;
        case SystemEvent::SessionInactive:
            sessionActive = false;
            monitor.suspendForSystemEvent();
            break;
        case SystemEvent::DidWake:
            systemSleeping = false;
            tray.ensureVisible();
            if (sessionActive) {
                monitor.resumeAfterSystemEvent();
            }
            break;
        case SystemEvent::SessionActive:
            sessionActive = true;
            tray.ensureVisible();
            if (!systemSleeping) {
                monitor.resumeAfterSystemEvent();
            }
            break;
        case SystemEvent::DesktopRestored:
            tray.ensureVisible();
            break;
        case SystemEvent::WillPowerOff:
            monitor.suspendForSystemEvent();
            break;
        }
    });
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application, [&] {
        qCInfo(logApp) << "Swap Alert shutting down";
        systemEventMonitor.stop();
    });
    tray.show();
    monitor.start();
    return application.exec();
}
