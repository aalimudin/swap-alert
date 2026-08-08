#pragma once

#include "platform/IProcessService.hpp"

#include <QDialog>
#include <QHash>

class QTableWidget;
class QLabel;
class QPushButton;

class CleanupDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CleanupDialog(IProcessService& processService, QWidget* parent = nullptr);
    void refresh();

private:
    void terminateSelected(bool force);
    void updateSelectionState();

    IProcessService& m_processService;
    QTableWidget* m_table;
    QLabel* m_summaryLabel;
    QPushButton* m_quitButton;
    QPushButton* m_forceButton;
    QHash<qint64, QString> m_pendingQuitRequests;
};
