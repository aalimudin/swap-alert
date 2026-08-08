#pragma once

#include "core/AlertTier.hpp"
#include "core/SettingsStore.hpp"
#include "core/SwapInfo.hpp"

#include <QDialog>

class QLabel;
class QProgressBar;

class StatusDialog final : public QDialog {
    Q_OBJECT

public:
    explicit StatusDialog(SettingsStore& settings, QWidget* parent = nullptr);

public slots:
    void updateSample(const SwapInfo& info, AlertTier tier);
    void showReadError(const QString& message);
    void setMonitoringEnabled(bool enabled);

signals:
    void refreshRequested();
    void settingsRequested();
    void reviewRequested();

private:
    void refreshThresholdSummary();
    void renderCurrentState();

    SettingsStore& m_settings;
    QLabel* m_usageLabel;
    QLabel* m_detailLabel;
    QLabel* m_tierLabel;
    QLabel* m_stateLabel;
    QLabel* m_thresholdLabel;
    QProgressBar* m_progress;
    SwapInfo m_lastInfo;
    AlertTier m_lastTier = AlertTier::Normal;
    bool m_hasSample = false;
    bool m_monitoringEnabled = true;
    QString m_readError;
};

