#include "ui/SettingsDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
constexpr double gibibyte = 1024.0 * 1024.0 * 1024.0;

QDoubleSpinBox* thresholdSpinBox(QWidget* parent)
{
    auto* spinBox = new QDoubleSpinBox(parent);
    spinBox->setRange(0.1, 1024.0);
    spinBox->setDecimals(1);
    spinBox->setSingleStep(0.5);
    spinBox->setSuffix(QStringLiteral(" GB"));
    return spinBox;
}
}

SettingsDialog::SettingsDialog(SettingsStore& settings, IAutostartService& autostartService,
    INotificationService& notificationService, QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_autostartService(autostartService)
    , m_notificationService(notificationService)
    , m_tier1(thresholdSpinBox(this))
    , m_tier2(thresholdSpinBox(this))
    , m_tier3(thresholdSpinBox(this))
    , m_polling(new QSpinBox(this))
    , m_cooldown(new QSpinBox(this))
    , m_monitoring(new QCheckBox(QStringLiteral("Enable monitoring"), this))
    , m_startAtLogin(new QCheckBox(QStringLiteral("Start Swap Alert at login"), this))
    , m_notificationStatus(new QLabel(QStringLiteral("Checking notification permission…"), this))
    , m_notificationAction(new QPushButton(QStringLiteral("Request Permission"), this))
{
    setWindowTitle(QStringLiteral("Swap Alert Settings"));
    setMinimumWidth(430);

    m_polling->setRange(2, 300);
    m_polling->setSuffix(QStringLiteral(" seconds"));
    m_cooldown->setRange(1, 1440);
    m_cooldown->setSuffix(QStringLiteral(" minutes"));

    auto* thresholds = new QFormLayout;
    thresholds->addRow(QStringLiteral("Tier 1 notification:"), m_tier1);
    thresholds->addRow(QStringLiteral("Tier 2 warning:"), m_tier2);
    thresholds->addRow(QStringLiteral("Tier 3 cleanup:"), m_tier3);
    thresholds->addRow(QStringLiteral("Polling interval:"), m_polling);
    thresholds->addRow(QStringLiteral("Alert cooldown:"), m_cooldown);

    auto* testTier1 = new QPushButton(QStringLiteral("Test Tier 1"), this);
    auto* testTier2 = new QPushButton(QStringLiteral("Test Tier 2"), this);
    auto* testTier3 = new QPushButton(QStringLiteral("Test Tier 3"), this);
    testTier1->setObjectName(QStringLiteral("testTier1Button"));
    testTier2->setObjectName(QStringLiteral("testTier2Button"));
    testTier3->setObjectName(QStringLiteral("testTier3Button"));
    m_notificationStatus->setObjectName(QStringLiteral("notificationStatusLabel"));
    m_notificationAction->setObjectName(QStringLiteral("notificationActionButton"));
    auto* testButtons = new QHBoxLayout;
    testButtons->addWidget(testTier1);
    testButtons->addWidget(testTier2);
    testButtons->addWidget(testTier3);

    auto* notificationRow = new QHBoxLayout;
    notificationRow->addWidget(m_notificationStatus, 1);
    notificationRow->addWidget(m_notificationAction);

    auto* restoreButton = new QPushButton(QStringLiteral("Restore Defaults"), this);
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
    auto* bottom = new QHBoxLayout;
    bottom->addWidget(restoreButton);
    bottom->addStretch();
    bottom->addWidget(buttonBox);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(thresholds);
    layout->addSpacing(8);
    layout->addWidget(m_monitoring);
    layout->addWidget(m_startAtLogin);
    layout->addSpacing(8);
    layout->addLayout(notificationRow);
    layout->addSpacing(8);
    layout->addLayout(testButtons);
    layout->addSpacing(8);
    layout->addLayout(bottom);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::save);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(restoreButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
    connect(testTier1, &QPushButton::clicked, this, [this] { emit testAlertRequested(1); });
    connect(testTier2, &QPushButton::clicked, this, [this] { emit testAlertRequested(2); });
    connect(testTier3, &QPushButton::clicked, this, [this] { emit testAlertRequested(3); });
    connect(m_notificationAction, &QPushButton::clicked, this,
        &SettingsDialog::handleNotificationAction);

    reload();
}

void SettingsDialog::reload()
{
    m_tier1->setValue(static_cast<double>(m_settings.tier1Bytes()) / gibibyte);
    m_tier2->setValue(static_cast<double>(m_settings.tier2Bytes()) / gibibyte);
    m_tier3->setValue(static_cast<double>(m_settings.tier3Bytes()) / gibibyte);
    m_polling->setValue(m_settings.pollingSeconds());
    m_cooldown->setValue(m_settings.cooldownMinutes());
    m_monitoring->setChecked(m_settings.monitoringEnabled());
    m_startAtLogin->setChecked(m_autostartService.isEnabled());
    refreshNotificationStatus();
}

void SettingsDialog::save()
{
    if (!(m_tier1->value() < m_tier2->value() && m_tier2->value() < m_tier3->value())) {
        QMessageBox::warning(this, QStringLiteral("Invalid Thresholds"),
            QStringLiteral("Thresholds must satisfy Tier 1 < Tier 2 < Tier 3."));
        return;
    }

    m_settings.setConfiguration(
        static_cast<quint64>(m_tier1->value() * gibibyte),
        static_cast<quint64>(m_tier2->value() * gibibyte),
        static_cast<quint64>(m_tier3->value() * gibibyte),
        m_polling->value(), m_cooldown->value(), m_monitoring->isChecked());

    if (m_startAtLogin->isChecked() != m_autostartService.isEnabled()) {
        QString error;
        if (!m_autostartService.setEnabled(m_startAtLogin->isChecked(), error)) {
            QMessageBox::warning(this, QStringLiteral("Start at Login"), error);
            reload();
            return;
        }
    }
    accept();
}

void SettingsDialog::restoreDefaults()
{
    m_tier1->setValue(2.0);
    m_tier2->setValue(4.0);
    m_tier3->setValue(8.0);
    m_polling->setValue(10);
    m_cooldown->setValue(15);
    m_monitoring->setChecked(true);
    m_startAtLogin->setChecked(false);
}

void SettingsDialog::refreshNotificationStatus()
{
    QPointer<SettingsDialog> self(this);
    m_notificationService.authorizationStatus([self](const NotificationResult& result) {
        if (self) {
            self->updateNotificationStatus(result);
        }
    });
}

void SettingsDialog::handleNotificationAction()
{
    if (m_notificationAuthorization == NotificationAuthorizationStatus::Denied
        || m_notificationAuthorization == NotificationAuthorizationStatus::Authorized) {
        QString error;
        if (!m_notificationService.openNotificationSettings(error)) {
            QMessageBox::warning(this, QStringLiteral("Notification Settings"), error);
        }
        return;
    }

    m_notificationAction->setEnabled(false);
    m_notificationStatus->setText(QStringLiteral("Requesting notification permission…"));
    QPointer<SettingsDialog> self(this);
    m_notificationService.requestAuthorization([self](const NotificationResult& result) {
        if (self) {
            self->updateNotificationStatus(result);
        }
    });
}

void SettingsDialog::updateNotificationStatus(const NotificationResult& result)
{
    m_notificationAuthorization = result.authorization;
    m_notificationAction->setEnabled(true);

    switch (result.authorization) {
    case NotificationAuthorizationStatus::Authorized:
        m_notificationStatus->setText(QStringLiteral("Notifications: Allowed"));
        m_notificationStatus->setStyleSheet(QStringLiteral("color: #218739;"));
        m_notificationAction->setText(QStringLiteral("Open Settings…"));
        break;
    case NotificationAuthorizationStatus::Denied:
        m_notificationStatus->setText(QStringLiteral("Notifications: Disabled"));
        m_notificationStatus->setStyleSheet(QStringLiteral("color: #c62828;"));
        m_notificationAction->setText(QStringLiteral("Open Settings…"));
        break;
    case NotificationAuthorizationStatus::NotDetermined:
        m_notificationStatus->setText(QStringLiteral("Notifications: Permission not requested"));
        m_notificationStatus->setStyleSheet({});
        m_notificationAction->setText(QStringLiteral("Request Permission"));
        break;
    case NotificationAuthorizationStatus::Unknown:
    default:
        m_notificationStatus->setText(result.message.isEmpty()
                ? QStringLiteral("Notifications: Status unavailable")
                : QStringLiteral("Notifications: %1").arg(result.message));
        m_notificationStatus->setStyleSheet(QStringLiteral("color: #c62828;"));
        m_notificationAction->setText(QStringLiteral("Try Again"));
        break;
    }
}
