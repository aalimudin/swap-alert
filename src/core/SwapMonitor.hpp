#pragma once

#include "core/AlertEngine.hpp"
#include "core/ISwapReader.hpp"
#include "core/SettingsStore.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>

class SwapMonitor final : public QObject {
    Q_OBJECT

public:
    SwapMonitor(std::unique_ptr<ISwapReader> reader, SettingsStore& settings,
        QObject* parent = nullptr);

    void start();
    void refreshNow();
    void snoozeForMinutes(int minutes);
    [[nodiscard]] bool isSnoozed() const;

signals:
    void sampleUpdated(const SwapInfo& info, AlertTier tier);
    void alertTriggered(AlertTier tier, const SwapInfo& info);
    void readFailed(const QString& message);

private slots:
    void applySettings();

private:
    std::unique_ptr<ISwapReader> m_reader;
    SettingsStore& m_settings;
    AlertEngine m_alertEngine;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_snoozedUntilMs = 0;
    bool m_started = false;
    bool m_snoozeActive = false;
    bool m_lastMonitoringEnabled = false;
};
