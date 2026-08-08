#include "ui/CleanupDialog.hpp"

#include "ui/Format.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int processIdRole = Qt::UserRole + 1;
}

CleanupDialog::CleanupDialog(IProcessService& processService, QWidget* parent)
    : QDialog(parent)
    , m_processService(processService)
    , m_table(new QTableWidget(this))
{
    setWindowTitle(QStringLiteral("Review Applications"));
    resize(640, 440);

    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Quit"), QStringLiteral("Application"), QStringLiteral("Memory")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    auto* hint = new QLabel(
        QStringLiteral("Save your work first. Swap Alert requests a normal quit unless you "
                       "explicitly choose Force Quit."),
        this);
    hint->setWordWrap(true);

    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    auto* quitButton = new QPushButton(QStringLiteral("Quit Selected"), this);
    auto* forceButton = new QPushButton(QStringLiteral("Force Quit Selected…"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(refreshButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    buttons->addWidget(forceButton);
    buttons->addWidget(quitButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(refreshButton, &QPushButton::clicked, this, &CleanupDialog::refresh);
    connect(quitButton, &QPushButton::clicked, this, [this] { terminateSelected(false); });
    connect(forceButton, &QPushButton::clicked, this, [this] { terminateSelected(true); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
}

void CleanupDialog::refresh()
{
    const auto applications = m_processService.runningApplications();
    m_table->setRowCount(applications.size());
    for (qsizetype row = 0; row < applications.size(); ++row) {
        const auto& application = applications[row];
        auto* checkbox = new QTableWidgetItem;
        checkbox->setCheckState(Qt::Unchecked);
        checkbox->setData(processIdRole, application.processId);
        auto* name = new QTableWidgetItem(application.name);
        auto* memory = new QTableWidgetItem(formatBytes(application.memoryBytes));
        memory->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(static_cast<int>(row), 0, checkbox);
        m_table->setItem(static_cast<int>(row), 1, name);
        m_table->setItem(static_cast<int>(row), 2, memory);
    }
}

void CleanupDialog::terminateSelected(bool force)
{
    QVector<qint64> processIds;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* item = m_table->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            processIds.push_back(item->data(processIdRole).toLongLong());
        }
    }

    if (processIds.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Applications Selected"),
            QStringLiteral("Select at least one application first."));
        return;
    }

    if (force
        && QMessageBox::warning(this, QStringLiteral("Force Quit Applications?"),
               QStringLiteral("Unsaved work in the selected applications will be lost. "
                              "Force quit them now?"),
               QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel)
            != QMessageBox::Yes) {
        return;
    }

    QStringList failures;
    for (qint64 processId : processIds) {
        QString error;
        if (!m_processService.terminate(processId, force, error)) {
            failures.push_back(error);
        }
    }

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Some Requests Failed"), failures.join('\n'));
    }
    refresh();
}
