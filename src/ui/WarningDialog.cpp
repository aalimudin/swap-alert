#include "ui/WarningDialog.hpp"

#include "ui/Format.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

WarningDialog::WarningDialog(QWidget* parent)
    : QDialog(parent)
    , m_titleLabel(new QLabel(this))
    , m_usageLabel(new QLabel(this))
{
    setWindowTitle(QStringLiteral("Swap Alert"));
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setMinimumWidth(420);

    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    auto* explanation = new QLabel(
        QStringLiteral("High swap usage can make the system feel slow. Review running "
                       "applications and save your work before quitting anything."),
        this);
    explanation->setWordWrap(true);

    auto* reviewButton = new QPushButton(QStringLiteral("Review Applications"), this);
    auto* snoozeButton = new QPushButton(QStringLiteral("Snooze 15 Minutes"), this);
    auto* dismissButton = new QPushButton(QStringLiteral("Dismiss"), this);
    reviewButton->setDefault(true);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(snoozeButton);
    buttons->addStretch();
    buttons->addWidget(dismissButton);
    buttons->addWidget(reviewButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_usageLabel);
    layout->addWidget(explanation);
    layout->addSpacing(8);
    layout->addLayout(buttons);

    connect(reviewButton, &QPushButton::clicked, this, [this] {
        hide();
        emit reviewRequested();
    });
    connect(snoozeButton, &QPushButton::clicked, this, [this] {
        hide();
        emit snoozeRequested(15);
    });
    connect(dismissButton, &QPushButton::clicked, this, &QDialog::hide);
}

void WarningDialog::showWarning(AlertTier tier, const SwapInfo& info)
{
    m_titleLabel->setText(tier == AlertTier::Tier3
            ? QStringLiteral("Critical swap usage")
            : QStringLiteral("High swap usage"));
    m_usageLabel->setText(QStringLiteral("Currently using %1 of %2 swap.")
                              .arg(formatBytes(info.usedBytes), formatBytes(info.totalBytes)));
    show();
    raise();
    activateWindow();
    QApplication::alert(this);
}

