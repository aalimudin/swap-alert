#pragma once

#include "core/SettingsStore.hpp"
#include "platform/IAutostartService.hpp"
#include "platform/INotificationService.hpp"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(SettingsStore& settings, IAutostartService& autostartService,
        INotificationService& notificationService, QWidget* parent = nullptr);
    void reload();

signals:
    void testAlertRequested(int tier);

private:
    void save();
    void restoreDefaults();
    void refreshNotificationStatus();
    void handleNotificationAction();
    void updateNotificationStatus(const NotificationResult& result);

    SettingsStore& m_settings;
    IAutostartService& m_autostartService;
    INotificationService& m_notificationService;
    QDoubleSpinBox* m_tier1;
    QDoubleSpinBox* m_tier2;
    QDoubleSpinBox* m_tier3;
    QSpinBox* m_polling;
    QSpinBox* m_cooldown;
    QCheckBox* m_monitoring;
    QCheckBox* m_startAtLogin;
    QLabel* m_notificationStatus;
    QPushButton* m_notificationAction;
    NotificationAuthorizationStatus m_notificationAuthorization =
        NotificationAuthorizationStatus::Unknown;
};
