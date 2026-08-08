#pragma once

#include "core/AlertTier.hpp"
#include "core/SettingsStore.hpp"
#include "core/SwapInfo.hpp"

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayController final : public QObject {
    Q_OBJECT

public:
    explicit TrayController(SettingsStore& settings, QObject* parent = nullptr);
    ~TrayController() override;
    void show();
    void ensureVisible();
    void updateSample(const SwapInfo& info, AlertTier tier);
    void showReadError(const QString& message);

signals:
    void dashboardRequested();
    void settingsRequested();
    void logsRequested();
    void reviewRequested();
    void refreshRequested();
    void snoozeRequested(int minutes);
    void snoozeUntilRestartRequested();
    void monitoringToggled(bool enabled);
    void quitRequested();

private:
    void rebuildIcon(AlertTier tier);

    SettingsStore& m_settings;
    QSystemTrayIcon* m_trayIcon;
    QMenu* m_menu;
    QAction* m_statusAction;
    QAction* m_tierAction;
    QAction* m_monitoringAction;
};
