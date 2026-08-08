#include "core/SettingsStore.hpp"
#include "core/SwapMonitor.hpp"
#include "platform/macos/MacAutostartService.hpp"
#include "platform/macos/MacNotificationService.hpp"
#include "platform/macos/MacProcessService.hpp"
#include "platform/macos/MacSwapReader.hpp"
#include "ui/CleanupDialog.hpp"
#include "ui/Format.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/TrayController.hpp"
#include "ui/WarningDialog.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>
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
}

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Swap Alert"));
    QApplication::setOrganizationName(QStringLiteral("SwapAlert"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("Swap Alert"),
            QStringLiteral("The macOS menu bar is unavailable."));
        return 1;
    }

    SettingsStore settings;
    MacAutostartService autostartService;
    MacNotificationService notificationService;
    MacProcessService processService;
    SwapMonitor monitor(std::make_unique<MacSwapReader>(), settings);
    TrayController tray(settings);
    SettingsDialog settingsDialog(settings, autostartService);
    WarningDialog warningDialog;
    CleanupDialog cleanupDialog(processService);

    QObject::connect(&tray, &TrayController::settingsRequested, &settingsDialog, [&] {
        settingsDialog.reload();
        settingsDialog.show();
        settingsDialog.raise();
        settingsDialog.activateWindow();
    });
    QObject::connect(&tray, &TrayController::reviewRequested, &cleanupDialog, [&] {
        cleanupDialog.refresh();
        cleanupDialog.show();
        cleanupDialog.raise();
        cleanupDialog.activateWindow();
    });
    QObject::connect(&tray, &TrayController::snoozeRequested, &monitor,
        &SwapMonitor::snoozeForMinutes);
    QObject::connect(&tray, &TrayController::monitoringToggled, &settings,
        &SettingsStore::setMonitoringEnabled);
    QObject::connect(&tray, &TrayController::quitRequested, &application,
        &QApplication::quit);

    QObject::connect(&monitor, &SwapMonitor::sampleUpdated, &tray,
        &TrayController::updateSample);
    QObject::connect(&monitor, &SwapMonitor::readFailed, &tray,
        &TrayController::showReadError);
    QObject::connect(&monitor, &SwapMonitor::alertTriggered, &application,
        [&](AlertTier tier, const SwapInfo& info) {
            const QString body = QStringLiteral("Swap usage has reached %1 of %2.")
                                     .arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes));
            notificationService.send(notificationTitle(tier), body, tier);
            if (tier == AlertTier::Tier2) {
                warningDialog.showWarning(tier, info);
            } else if (tier == AlertTier::Tier3) {
                cleanupDialog.refresh();
                cleanupDialog.show();
                cleanupDialog.raise();
                cleanupDialog.activateWindow();
                QApplication::alert(&cleanupDialog);
            }
        });

    QObject::connect(&warningDialog, &WarningDialog::reviewRequested, &cleanupDialog, [&] {
        cleanupDialog.refresh();
        cleanupDialog.show();
        cleanupDialog.raise();
        cleanupDialog.activateWindow();
    });
    QObject::connect(&warningDialog, &WarningDialog::snoozeRequested, &monitor,
        &SwapMonitor::snoozeForMinutes);

    QObject::connect(&settingsDialog, &SettingsDialog::testAlertRequested, &application,
        [&](int requestedTier) {
            const auto tier = static_cast<AlertTier>(requestedTier);
            SwapInfo example {16ULL * 1024 * 1024 * 1024,
                static_cast<quint64>(requestedTier * 3ULL) * 1024 * 1024 * 1024,
                0};
            notificationService.send(notificationTitle(tier),
                QStringLiteral("This is a Tier %1 test alert.").arg(requestedTier), tier);
            if (tier == AlertTier::Tier2) {
                warningDialog.showWarning(tier, example);
            } else if (tier == AlertTier::Tier3) {
                cleanupDialog.refresh();
                cleanupDialog.show();
                cleanupDialog.raise();
                cleanupDialog.activateWindow();
            }
        });

    notificationService.requestAuthorization();
    tray.show();
    monitor.start();
    return application.exec();
}

