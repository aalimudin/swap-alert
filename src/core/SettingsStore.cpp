#include "core/SettingsStore.hpp"

namespace {
constexpr quint64 gibibyte = 1024ULL * 1024ULL * 1024ULL;
}

SettingsStore::SettingsStore(QObject* parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope, "SwapAlert", "SwapAlert")
{
}

quint64 SettingsStore::tier1Bytes() const
{
    return m_settings.value("thresholds/tier1Bytes", 2ULL * gibibyte).toULongLong();
}

quint64 SettingsStore::tier2Bytes() const
{
    return m_settings.value("thresholds/tier2Bytes", 4ULL * gibibyte).toULongLong();
}

quint64 SettingsStore::tier3Bytes() const
{
    return m_settings.value("thresholds/tier3Bytes", 8ULL * gibibyte).toULongLong();
}

int SettingsStore::pollingSeconds() const
{
    return m_settings.value("monitoring/pollingSeconds", 10).toInt();
}

int SettingsStore::cooldownMinutes() const
{
    return m_settings.value("alerts/cooldownMinutes", 15).toInt();
}

double SettingsStore::resetMargin() const
{
    return m_settings.value("alerts/resetMargin", 0.15).toDouble();
}

bool SettingsStore::monitoringEnabled() const
{
    return m_settings.value("monitoring/enabled", true).toBool();
}

void SettingsStore::setThresholds(quint64 tier1, quint64 tier2, quint64 tier3)
{
    m_settings.setValue("thresholds/tier1Bytes", tier1);
    m_settings.setValue("thresholds/tier2Bytes", tier2);
    m_settings.setValue("thresholds/tier3Bytes", tier3);
    emit changed();
}

void SettingsStore::setPollingSeconds(int seconds)
{
    m_settings.setValue("monitoring/pollingSeconds", seconds);
    emit changed();
}

void SettingsStore::setCooldownMinutes(int minutes)
{
    m_settings.setValue("alerts/cooldownMinutes", minutes);
    emit changed();
}

void SettingsStore::setMonitoringEnabled(bool enabled)
{
    m_settings.setValue("monitoring/enabled", enabled);
    emit changed();
}

void SettingsStore::restoreDefaults()
{
    m_settings.clear();
    emit changed();
}

AlertEngine::Configuration SettingsStore::alertConfiguration() const
{
    return {
        {tier1Bytes(), tier2Bytes(), tier3Bytes()},
        static_cast<qint64>(cooldownMinutes()) * 60 * 1000,
        resetMargin(),
    };
}

