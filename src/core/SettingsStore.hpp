#pragma once

#include "core/AlertEngine.hpp"

#include <QObject>
#include <QSettings>

class SettingsStore final : public QObject {
    Q_OBJECT

public:
    explicit SettingsStore(QObject* parent = nullptr);

    [[nodiscard]] quint64 tier1Bytes() const;
    [[nodiscard]] quint64 tier2Bytes() const;
    [[nodiscard]] quint64 tier3Bytes() const;
    [[nodiscard]] int pollingSeconds() const;
    [[nodiscard]] int cooldownMinutes() const;
    [[nodiscard]] double resetMargin() const;
    [[nodiscard]] bool monitoringEnabled() const;

    void setThresholds(quint64 tier1, quint64 tier2, quint64 tier3);
    void setPollingSeconds(int seconds);
    void setCooldownMinutes(int minutes);
    void setMonitoringEnabled(bool enabled);
    void restoreDefaults();

    [[nodiscard]] AlertEngine::Configuration alertConfiguration() const;

signals:
    void changed();

private:
    QSettings m_settings;
};

