#pragma once

#include "platform/IProcessService.hpp"

#include <QDialog>

class QTableWidget;

class CleanupDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CleanupDialog(IProcessService& processService, QWidget* parent = nullptr);
    void refresh();

private:
    void terminateSelected(bool force);

    IProcessService& m_processService;
    QTableWidget* m_table;
};

