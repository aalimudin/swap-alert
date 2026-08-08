#include "ui/StatusDialog.hpp"

#include "ui/Format.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString tierName(AlertTier tier)
{
    switch (tier) {
    case AlertTier::Tier1:
        return QStringLiteral("Tier 1 — Notification");
    case AlertTier::Tier2:
        return QStringLiteral("Tier 2 — High");
    case AlertTier::Tier3:
        return QStringLiteral("Tier 3 — Critical");
    case AlertTier::Normal:
    default:
        return QStringLiteral("Normal");
    }
}

QString tierColor(AlertTier tier)
{
    switch (tier) {
    case AlertTier::Tier1:
        return QStringLiteral("#9a7300");
    case AlertTier::Tier2:
        return QStringLiteral("#c45d00");
    case AlertTier::Tier3:
        return QStringLiteral("#c62828");
    case AlertTier::Normal:
    default:
        return QStringLiteral("#218739");
    }
}
}

StatusDialog::StatusDialog(SettingsStore& settings, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_usageLabel(new QLabel(QStringLiteral("Waiting for a reading…"), this))
    , m_detailLabel(new QLabel(this))
    , m_tierLabel(new QLabel(QStringLiteral("Normal"), this))
    , m_stateLabel(new QLabel(QStringLiteral("Monitoring active"), this))
    , m_thresholdLabel(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_monitoringEnabled(settings.monitoringEnabled())
{
    setWindowTitle(QStringLiteral("Swap Alert Dashboard"));
    setMinimumWidth(480);

    QFont usageFont = m_usageLabel->font();
    usageFont.setPointSize(usageFont.pointSize() + 8);
    usageFont.setBold(true);
    m_usageLabel->setFont(usageFont);

    QFont tierFont = m_tierLabel->font();
    tierFont.setBold(true);
    m_tierLabel->setFont(tierFont);

    m_progress->setRange(0, 1000);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_thresholdLabel->setWordWrap(true);
    m_stateLabel->setWordWrap(true);

    auto* refreshButton = new QPushButton(QStringLiteral("Refresh Now"), this);
    auto* reviewButton = new QPushButton(QStringLiteral("Review Applications…"), this);
    auto* settingsButton = new QPushButton(QStringLiteral("Settings…"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);

    auto* actions = new QHBoxLayout;
    actions->addWidget(refreshButton);
    actions->addWidget(reviewButton);
    actions->addStretch();
    actions->addWidget(settingsButton);
    actions->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_usageLabel);
    layout->addWidget(m_detailLabel);
    layout->addWidget(m_progress);
    layout->addSpacing(6);
    layout->addWidget(m_tierLabel);
    layout->addWidget(m_stateLabel);
    layout->addSpacing(8);
    layout->addWidget(m_thresholdLabel);
    layout->addSpacing(8);
    layout->addLayout(actions);

    connect(refreshButton, &QPushButton::clicked, this, &StatusDialog::refreshRequested);
    connect(reviewButton, &QPushButton::clicked, this, &StatusDialog::reviewRequested);
    connect(settingsButton, &QPushButton::clicked, this, &StatusDialog::settingsRequested);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(&m_settings, &SettingsStore::changed, this, [this] {
        m_monitoringEnabled = m_settings.monitoringEnabled();
        refreshThresholdSummary();
        renderCurrentState();
    });

    refreshThresholdSummary();
    renderCurrentState();
}

void StatusDialog::updateSample(const SwapInfo& info, AlertTier tier)
{
    m_lastInfo = info;
    m_lastTier = tier;
    m_hasSample = true;
    m_readError.clear();
    renderCurrentState();
}

void StatusDialog::showReadError(const QString& message)
{
    m_readError = message;
    renderCurrentState();
}

void StatusDialog::setMonitoringEnabled(bool enabled)
{
    m_monitoringEnabled = enabled;
    renderCurrentState();
}

void StatusDialog::refreshThresholdSummary()
{
    m_thresholdLabel->setText(QStringLiteral("Alert thresholds: %1 · %2 · %3")
                                  .arg(formatBytes(m_settings.tier1Bytes()),
                                      formatBytes(m_settings.tier2Bytes()),
                                      formatBytes(m_settings.tier3Bytes())));
}

void StatusDialog::renderCurrentState()
{
    if (!m_monitoringEnabled) {
        m_usageLabel->setText(m_hasSample ? formatBytes(m_lastInfo.usedBytes)
                                         : QStringLiteral("Monitoring paused"));
        m_detailLabel->setText(m_hasSample
                ? QStringLiteral("Last reading: %1 of %2")
                      .arg(formatBytes(m_lastInfo.usedBytes), formatBytes(m_lastInfo.totalBytes))
                : QString());
        m_tierLabel->setText(QStringLiteral("Paused"));
        m_tierLabel->setStyleSheet(QStringLiteral("color: #777777;"));
        m_stateLabel->setText(QStringLiteral("Monitoring is paused. Resume it from the menu bar or Settings."));
        m_progress->setEnabled(false);
        return;
    }

    m_progress->setEnabled(true);
    if (!m_readError.isEmpty()) {
        m_progress->setEnabled(false);
        m_usageLabel->setText(QStringLiteral("Swap unavailable"));
        m_detailLabel->clear();
        m_tierLabel->setText(QStringLiteral("Unavailable"));
        m_tierLabel->setStyleSheet(QStringLiteral("color: #777777;"));
        m_stateLabel->setText(m_readError);
        return;
    }

    if (!m_hasSample) {
        m_progress->setEnabled(false);
        m_usageLabel->setText(QStringLiteral("Waiting for a reading…"));
        m_detailLabel->clear();
        m_tierLabel->setText(QStringLiteral("Normal"));
        m_tierLabel->setStyleSheet(QStringLiteral("color: #218739;"));
        m_stateLabel->setText(QStringLiteral("Monitoring active"));
        return;
    }

    m_usageLabel->setText(formatBytes(m_lastInfo.usedBytes));
    m_progress->setEnabled(true);
    m_detailLabel->setText(QStringLiteral("%1 used of %2 available swap capacity")
                               .arg(formatBytes(m_lastInfo.usedBytes),
                                   formatBytes(m_lastInfo.totalBytes)));
    const int progressValue = m_lastInfo.totalBytes == 0
        ? 0
        : qBound(0, static_cast<int>(
                        static_cast<double>(m_lastInfo.usedBytes)
                        / static_cast<double>(m_lastInfo.totalBytes) * 1000.0),
              1000);
    m_progress->setValue(progressValue);
    m_progress->setFormat(QStringLiteral("%1%").arg(progressValue / 10.0, 0, 'f', 1));
    m_tierLabel->setText(tierName(m_lastTier));
    m_tierLabel->setStyleSheet(
        QStringLiteral("color: %1;").arg(tierColor(m_lastTier)));
    m_stateLabel->setText(QStringLiteral("Monitoring active · updates every %1 seconds")
                              .arg(m_settings.pollingSeconds()));
}
