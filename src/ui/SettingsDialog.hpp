#pragma once

#include "core/SettingsStore.hpp"
#include "platform/IAutostartService.hpp"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(SettingsStore& settings, IAutostartService& autostartService,
        QWidget* parent = nullptr);
    void reload();

signals:
    void testAlertRequested(int tier);

private:
    void save();
    void restoreDefaults();

    SettingsStore& m_settings;
    IAutostartService& m_autostartService;
    QDoubleSpinBox* m_tier1;
    QDoubleSpinBox* m_tier2;
    QDoubleSpinBox* m_tier3;
    QSpinBox* m_polling;
    QSpinBox* m_cooldown;
    QCheckBox* m_monitoring;
    QCheckBox* m_startAtLogin;
};

