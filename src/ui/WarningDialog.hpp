#pragma once

#include "core/AlertTier.hpp"
#include "core/SwapInfo.hpp"

#include <QDialog>

class QLabel;

class WarningDialog final : public QDialog {
    Q_OBJECT

public:
    explicit WarningDialog(QWidget* parent = nullptr);
    void showWarning(AlertTier tier, const SwapInfo& info,
        const QString& supplementalMessage = {});

signals:
    void reviewRequested();
    void snoozeRequested(int minutes);

private:
    QLabel* m_titleLabel;
    QLabel* m_usageLabel;
    QLabel* m_explanationLabel;
};
