#include "core/SwapMonitor.hpp"

SwapMonitor::SwapMonitor(std::unique_ptr<ISwapReader> reader, SettingsStore& settings,
    std::unique_ptr<IMonotonicClock> clock, QObject* parent)
    : QObject(parent)
    , m_reader(std::move(reader))
    , m_clock(std::move(clock))
    , m_settings(settings)
    , m_alertEngine(settings.alertConfiguration())
    , m_lastMonitoringEnabled(settings.monitoringEnabled())
{
    connect(&m_timer, &QTimer::timeout, this, &SwapMonitor::refreshNow);
    connect(&m_settings, &SettingsStore::changed, this, &SwapMonitor::applySettings);
    applySettings();
}

void SwapMonitor::start()
{
    m_started = true;
    if (m_settings.monitoringEnabled()) {
        m_timer.start();
        refreshNow();
    }
}

void SwapMonitor::refreshNow()
{
    if (!m_settings.monitoringEnabled()) {
        return;
    }

    QString error;
    const auto info = m_reader->read(error);
    if (!info) {
        emit readFailed(error);
        return;
    }

    if (m_snoozeActive && !isSnoozed()) {
        m_snoozeActive = false;
        m_alertEngine.reset();
    }

    const auto evaluation = m_alertEngine.evaluate(info->usedBytes, m_clock->nowMs());
    emit sampleUpdated(*info, evaluation.currentTier);
    if (evaluation.triggeredTier && !isSnoozed()) {
        emit alertTriggered(*evaluation.triggeredTier, *info);
    }
}

void SwapMonitor::snoozeForMinutes(int minutes)
{
    m_snoozedUntilMs = m_clock->nowMs() + static_cast<qint64>(minutes) * 60 * 1000;
    m_snoozeActive = true;
    m_snoozedUntilRestart = false;
}

void SwapMonitor::snoozeUntilRestart()
{
    m_snoozeActive = true;
    m_snoozedUntilRestart = true;
}

bool SwapMonitor::isSnoozed() const
{
    return m_snoozedUntilRestart || m_clock->nowMs() < m_snoozedUntilMs;
}

void SwapMonitor::applySettings()
{
    m_alertEngine.configure(m_settings.alertConfiguration());
    m_timer.setInterval(m_settings.pollingSeconds() * 1000);
    if (!m_lastMonitoringEnabled && m_settings.monitoringEnabled()) {
        m_alertEngine.reset();
    }
    m_lastMonitoringEnabled = m_settings.monitoringEnabled();
    if (!m_started) {
        return;
    }
    if (m_settings.monitoringEnabled()) {
        if (!m_timer.isActive()) {
            m_timer.start();
        }
        refreshNow();
    } else {
        m_timer.stop();
    }
}
