#include "core/SwapMonitor.hpp"

#include "core/Logging.hpp"

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
    qCInfo(logMonitor) << "Monitor started; polling interval"
                       << m_settings.pollingSeconds() << "seconds";
    if (m_settings.monitoringEnabled() && !m_systemSuspended) {
        m_timer.start();
        refreshNow();
    }
}

void SwapMonitor::refreshNow()
{
    if (!m_settings.monitoringEnabled() || m_systemSuspended) {
        return;
    }

    QString error;
    const auto info = m_reader->read(error);
    if (!info) {
        qCWarning(logMonitor) << "Swap reading failed:" << error;
        emit readFailed(error);
        return;
    }

    if (m_snoozeActive && !isSnoozed()) {
        m_snoozeActive = false;
        m_alertEngine.reset();
    }

    const auto evaluation = m_alertEngine.evaluate(info->usedBytes, m_clock->nowMs());
    qCDebug(logMonitor) << "Swap sample" << info->usedBytes << "of" << info->totalBytes
                        << "bytes; tier" << static_cast<int>(evaluation.currentTier);
    emit sampleUpdated(*info, evaluation.currentTier);
    if (evaluation.triggeredTier && !isSnoozed()) {
        qCInfo(logAlerts) << "Alert tier" << static_cast<int>(*evaluation.triggeredTier)
                          << "triggered at" << info->usedBytes << "bytes";
        emit alertTriggered(*evaluation.triggeredTier, *info);
    }
}

void SwapMonitor::suspendForSystemEvent()
{
    if (m_systemSuspended) {
        return;
    }
    m_systemSuspended = true;
    m_timer.stop();
    qCInfo(logMonitor) << "Monitoring suspended for a system event";
}

void SwapMonitor::resumeAfterSystemEvent()
{
    if (!m_systemSuspended) {
        return;
    }
    m_systemSuspended = false;
    qCInfo(logMonitor) << "Monitoring resumed after a system event";
    if (m_started && m_settings.monitoringEnabled()) {
        m_timer.start();
        refreshNow();
    }
}

void SwapMonitor::snoozeForMinutes(int minutes)
{
    m_snoozedUntilMs = m_clock->nowMs() + static_cast<qint64>(minutes) * 60 * 1000;
    m_snoozeActive = true;
    m_snoozedUntilRestart = false;
    qCInfo(logAlerts) << "Alerts snoozed for" << minutes << "minutes";
}

void SwapMonitor::snoozeUntilRestart()
{
    m_snoozeActive = true;
    m_snoozedUntilRestart = true;
    qCInfo(logAlerts) << "Alerts snoozed until the application restarts";
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
    if (m_systemSuspended) {
        m_timer.stop();
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
