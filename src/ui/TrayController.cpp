#include "ui/TrayController.hpp"

#include "ui/Format.hpp"

#include <QAction>
#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSystemTrayIcon>

namespace {
QColor colorForTier(AlertTier tier)
{
    switch (tier) {
    case AlertTier::Tier1:
        return QColor(QStringLiteral("#f4c430"));
    case AlertTier::Tier2:
        return QColor(QStringLiteral("#f28c28"));
    case AlertTier::Tier3:
        return QColor(QStringLiteral("#dc3545"));
    case AlertTier::Normal:
    default:
        return QColor(QStringLiteral("#2da44e"));
    }
}

QIcon statusIcon(AlertTier tier)
{
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colorForTier(tier));
    painter.drawEllipse(6, 6, 24, 24);
    return QIcon(pixmap);
}
}

TrayController::TrayController(SettingsStore& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_trayIcon(new QSystemTrayIcon(this))
    , m_menu(new QMenu)
    , m_statusAction(m_menu->addAction(QStringLiteral("Reading swap usage…")))
    , m_monitoringAction(nullptr)
{
    m_statusAction->setEnabled(false);
    m_menu->addSeparator();

    auto* reviewAction = m_menu->addAction(QStringLiteral("Review Applications…"));
    auto* snoozeMenu = m_menu->addMenu(QStringLiteral("Snooze Alerts"));
    auto* snooze15 = snoozeMenu->addAction(QStringLiteral("15 Minutes"));
    auto* snooze60 = snoozeMenu->addAction(QStringLiteral("1 Hour"));
    m_monitoringAction = m_menu->addAction(QStringLiteral("Monitoring Enabled"));
    m_monitoringAction->setCheckable(true);
    m_monitoringAction->setChecked(m_settings.monitoringEnabled());
    m_menu->addSeparator();
    auto* settingsAction = m_menu->addAction(QStringLiteral("Settings…"));
    auto* quitAction = m_menu->addAction(QStringLiteral("Quit Swap Alert"));

    m_trayIcon->setContextMenu(m_menu);
    m_trayIcon->setToolTip(QStringLiteral("Swap Alert"));
    rebuildIcon(AlertTier::Normal);

    connect(reviewAction, &QAction::triggered, this, &TrayController::reviewRequested);
    connect(snooze15, &QAction::triggered, this, [this] { emit snoozeRequested(15); });
    connect(snooze60, &QAction::triggered, this, [this] { emit snoozeRequested(60); });
    connect(settingsAction, &QAction::triggered, this, &TrayController::settingsRequested);
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);
    connect(m_monitoringAction, &QAction::toggled, this, &TrayController::monitoringToggled);
    connect(&m_settings, &SettingsStore::changed, this, [this] {
        const QSignalBlocker blocker(m_monitoringAction);
        m_monitoringAction->setChecked(m_settings.monitoringEnabled());
    });
}

void TrayController::show()
{
    m_trayIcon->show();
}

void TrayController::updateSample(const SwapInfo& info, AlertTier tier)
{
    const QString status = QStringLiteral("Swap: %1 of %2")
                               .arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes));
    m_statusAction->setText(status);
    m_trayIcon->setToolTip(QStringLiteral("Swap Alert — %1").arg(status));
    rebuildIcon(tier);
}

void TrayController::showReadError(const QString& message)
{
    m_statusAction->setText(QStringLiteral("Swap usage unavailable"));
    m_trayIcon->setToolTip(message);
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#8c8c8c")));
    painter.drawEllipse(6, 6, 24, 24);
    m_trayIcon->setIcon(QIcon(pixmap));
}

void TrayController::rebuildIcon(AlertTier tier)
{
    m_trayIcon->setIcon(statusIcon(tier));
}
