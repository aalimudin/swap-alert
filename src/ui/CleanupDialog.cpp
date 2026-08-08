#include "ui/CleanupDialog.hpp"

#include "ui/Format.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int processIdRole = Qt::UserRole + 1;
constexpr int memoryBytesRole = Qt::UserRole + 2;
constexpr int bundleIdentifierRole = Qt::UserRole + 3;

class NumericTableItem final : public QTableWidgetItem {
public:
    NumericTableItem(const QString& displayText, quint64 numericValue)
        : QTableWidgetItem(displayText)
    {
        setData(memoryBytesRole, numericValue);
    }

    bool operator<(const QTableWidgetItem& other) const override
    {
        return data(memoryBytesRole).toULongLong() < other.data(memoryBytesRole).toULongLong();
    }
};
}

CleanupDialog::CleanupDialog(IProcessService& processService, QWidget* parent)
    : QDialog(parent)
    , m_processService(processService)
    , m_table(new QTableWidget(this))
    , m_summaryLabel(new QLabel(this))
    , m_quitButton(new QPushButton(QStringLiteral("Quit Selected"), this))
    , m_forceButton(new QPushButton(QStringLiteral("Force Quit Selected…"), this))
{
    setWindowTitle(QStringLiteral("Review Applications"));
    resize(640, 440);
    m_table->setObjectName(QStringLiteral("applicationTable"));
    m_quitButton->setObjectName(QStringLiteral("quitSelectedButton"));
    m_forceButton->setObjectName(QStringLiteral("forceQuitSelectedButton"));

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("Select"), QStringLiteral("Application"), QStringLiteral("Processes"),
            QStringLiteral("Memory"), QStringLiteral("Status")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    auto* hint = new QLabel(
        QStringLiteral("Save your work first. Swap Alert requests a normal quit unless you "
                       "explicitly choose Force Quit."),
        this);
    hint->setWordWrap(true);

    auto* refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    auto* closeButton = new QPushButton(QStringLiteral("Close"), this);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(refreshButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    buttons->addWidget(m_forceButton);
    buttons->addWidget(m_quitButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_summaryLabel);
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(refreshButton, &QPushButton::clicked, this, &CleanupDialog::refresh);
    connect(m_quitButton, &QPushButton::clicked, this, [this] { terminateSelected(false); });
    connect(m_forceButton, &QPushButton::clicked, this, [this] { terminateSelected(true); });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::hide);
    connect(m_table, &QTableWidget::itemChanged, this, &CleanupDialog::updateSelectionState);
    updateSelectionState();
}

void CleanupDialog::refresh()
{
    QSet<qint64> selectedProcessIds;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* selection = m_table->item(row, 0);
        if (selection && selection->checkState() == Qt::Checked) {
            selectedProcessIds.insert(selection->data(processIdRole).toLongLong());
        }
    }

    const auto applications = m_processService.runningApplications();
    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(applications.size());
    quint64 totalMemoryBytes = 0;
    QHash<qint64, QString> runningIdentities;
    for (qsizetype row = 0; row < applications.size(); ++row) {
        const auto& application = applications[row];
        runningIdentities.insert(application.processId, application.bundleIdentifier);
        totalMemoryBytes += application.memoryBytes;
        auto* checkbox = new QTableWidgetItem;
        checkbox->setCheckState(selectedProcessIds.contains(application.processId)
                ? Qt::Checked
                : Qt::Unchecked);
        checkbox->setData(processIdRole, application.processId);
        checkbox->setData(bundleIdentifierRole, application.bundleIdentifier);
        auto* name = new QTableWidgetItem(application.name);
        name->setToolTip(application.bundleIdentifier);
        auto* processCount = new NumericTableItem(
            QString::number(application.processCount), application.processCount);
        auto* memory = new NumericTableItem(
            formatBytes(application.memoryBytes), application.memoryBytes);
        processCount->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        memory->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(static_cast<int>(row), 0, checkbox);
        m_table->setItem(static_cast<int>(row), 1, name);
        m_table->setItem(static_cast<int>(row), 2, processCount);
        m_table->setItem(static_cast<int>(row), 3, memory);
        auto* status = new QTableWidgetItem;
        if (m_pendingQuitRequests.contains(application.processId)
            && m_pendingQuitRequests.value(application.processId) == application.bundleIdentifier) {
            status->setText(QStringLiteral("Still running"));
        }
        m_table->setItem(static_cast<int>(row), 4, status);
    }
    for (auto request = m_pendingQuitRequests.begin(); request != m_pendingQuitRequests.end();) {
        if (!runningIdentities.contains(request.key())
            || runningIdentities.value(request.key()) != request.value()) {
            request = m_pendingQuitRequests.erase(request);
        } else {
            ++request;
        }
    }
    m_table->setSortingEnabled(true);
    m_table->sortItems(3, Qt::DescendingOrder);
    m_summaryLabel->setText(applications.isEmpty()
            ? QStringLiteral("No eligible user applications are currently running.")
            : QStringLiteral("%1 eligible applications · approximately %2 total memory")
                  .arg(applications.size())
                  .arg(formatBytes(totalMemoryBytes)));
    updateSelectionState();
}

void CleanupDialog::terminateSelected(bool force)
{
    struct SelectedApplication {
        qint64 processId;
        QString name;
        QString bundleIdentifier;
        int row;
    };
    QVector<SelectedApplication> selected;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* item = m_table->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            selected.push_back({item->data(processIdRole).toLongLong(),
                m_table->item(row, 1)->text(), item->data(bundleIdentifierRole).toString(), row});
        }
    }

    if (selected.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("No Applications Selected"),
            QStringLiteral("Select at least one application first."));
        return;
    }

    if (force
        && QMessageBox::warning(this, QStringLiteral("Force Quit Applications?"),
               QStringLiteral("Unsaved work in %1 selected application(s) will be lost. "
                              "Force quit them now?")
                   .arg(selected.size()),
               QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel)
            != QMessageBox::Yes) {
        return;
    }

    QStringList failures;
    for (const auto& application : selected) {
        QString error;
        if (!m_processService.terminate(application.processId, force, error)) {
            failures.push_back(QStringLiteral("%1: %2").arg(application.name, error));
            m_table->item(application.row, 4)->setText(QStringLiteral("Failed"));
        } else {
            if (force) {
                m_pendingQuitRequests.remove(application.processId);
            } else {
                m_pendingQuitRequests.insert(
                    application.processId, application.bundleIdentifier);
            }
            m_table->item(application.row, 4)->setText(
                force ? QStringLiteral("Force quit requested") : QStringLiteral("Quit requested"));
        }
    }

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Some Requests Failed"), failures.join('\n'));
    }
    m_quitButton->setEnabled(false);
    m_forceButton->setEnabled(false);
    QTimer::singleShot(force ? 750 : 1500, this, &CleanupDialog::refresh);
}

void CleanupDialog::updateSelectionState()
{
    int selectedCount = 0;
    bool allSelectedHaveQuitRequest = true;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const auto* item = m_table->item(row, 0);
        if (item && item->checkState() == Qt::Checked) {
            ++selectedCount;
            const qint64 processId = item->data(processIdRole).toLongLong();
            const QString bundleIdentifier = item->data(bundleIdentifierRole).toString();
            if (!m_pendingQuitRequests.contains(processId)
                || m_pendingQuitRequests.value(processId) != bundleIdentifier) {
                allSelectedHaveQuitRequest = false;
            }
        }
    }
    m_quitButton->setEnabled(selectedCount > 0);
    m_forceButton->setEnabled(selectedCount > 0 && allSelectedHaveQuitRequest);
    m_forceButton->setToolTip(selectedCount > 0 && !allSelectedHaveQuitRequest
            ? QStringLiteral("Request a normal quit first. Force Quit is enabled only if the application remains running.")
            : QString());
}
