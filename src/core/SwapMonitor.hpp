#pragma once

#include "core/AlertEngine.hpp"
#include "core/IMonotonicClock.hpp"
#include "core/ISwapReader.hpp"
#include "core/SettingsStore.hpp"

#include <QObject>
#include <QTimer>
#include <memory>

class SwapMonitor final : public QObject {
    Q_OBJECT

public:
    SwapMonitor(std::unique_ptr<ISwapReader> reader, SettingsStore& settings,
        std::unique_ptr<IMonotonicClock> clock, QObject* parent = nullptr);

    void start();
    void refreshNow();
    void suspendForSystemEvent();
    void resumeAfterSystemEvent();
    void snoozeForMinutes(int minutes);
    void snoozeUntilRestart();
    [[nodiscard]] bool isSnoozed() const;

signals:
    void sampleUpdated(const SwapInfo& info, AlertTier tier);
    void alertTriggered(AlertTier tier, const SwapInfo& info);
    void readFailed(const QString& message);

private slots:
    void applySettings();

private:
    std::unique_ptr<ISwapReader> m_reader;
    std::unique_ptr<IMonotonicClock> m_clock;
    SettingsStore& m_settings;
    AlertEngine m_alertEngine;
    QTimer m_timer;
    qint64 m_snoozedUntilMs = 0;
    bool m_started = false;
    bool m_snoozeActive = false;
    bool m_snoozedUntilRestart = false;
    bool m_lastMonitoringEnabled = false;
    bool m_systemSuspended = false;
};
