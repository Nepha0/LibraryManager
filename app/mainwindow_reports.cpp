#include "mainwindow.h"
#include "windowwidgets.h"

#include <QColor>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace WindowWidgets;

QWidget *MainWindow::buildDashboardPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *introRow = new QHBoxLayout;
    auto *intro = new QVBoxLayout;
    intro->addWidget(makeLabel(QStringLiteral("馆藏运营概览"),
                               QStringLiteral("sectionTitle")));
    intro->addWidget(makeLabel(
        QStringLiteral("借阅状态与关键数据会随业务操作实时更新。"),
        QStringLiteral("mutedText")));
    auto *addButton = makePrimaryButton(QStringLiteral("新增馆藏"));
    auto *borrowButton = new QPushButton(QStringLiteral("办理借阅"));
    borrowButton->setProperty("role", QStringLiteral("secondary"));
    introRow->addLayout(intro);
    introRow->addStretch();
    introRow->addWidget(addButton);
    introRow->addWidget(borrowButton);
    layout->addLayout(introRow);

    auto *metrics = new QHBoxLayout;
    metrics->setSpacing(12);
    metrics->addWidget(makeMetric(QStringLiteral("馆藏书目"), &m_titlesStat,
                                  QStringLiteral("blue")));
    metrics->addWidget(makeMetric(QStringLiteral("当前可借"), &m_availableStat,
                                  QStringLiteral("green")));
    metrics->addWidget(makeMetric(QStringLiteral("借阅中"), &m_activeLoansStat,
                                  QStringLiteral("orange")));
    metrics->addWidget(makeMetric(QStringLiteral("已逾期"), &m_overdueStat,
                                  QStringLiteral("red")));
    layout->addLayout(metrics);

    auto *recentPanel = makePanel();
    auto *recentLayout = new QVBoxLayout(recentPanel);
    recentLayout->setContentsMargins(20, 18, 20, 18);
    auto *recentHeader = new QHBoxLayout;
    recentHeader->addWidget(makeLabel(QStringLiteral("当前借阅"),
                                      QStringLiteral("panelTitle")));
    recentHeader->addStretch();
    recentHeader->addWidget(makeLabel(
        QStringLiteral("按应还时间排序"), QStringLiteral("mutedText")));
    recentLayout->addLayout(recentHeader);
    m_dashboardLoans = makeTable({
        QStringLiteral("读者"), QStringLiteral("读者编号"),
        QStringLiteral("馆藏"), QStringLiteral("册编号"),
        QStringLiteral("应还日期"), QStringLiteral("状态")
    });
    recentLayout->addWidget(m_dashboardLoans);
    layout->addWidget(recentPanel, 1);

    connect(addButton, &QPushButton::clicked, this, [this] {
        m_navigation->setCurrentRow(1);
        m_bookTitle->setFocus();
    });
    connect(borrowButton, &QPushButton::clicked, this, [this] {
        m_navigation->setCurrentRow(2);
        m_borrowReaderNo->setFocus();
    });
    return page;
}

QWidget *MainWindow::buildStatisticsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *summary = new QHBoxLayout;
    summary->setSpacing(12);
    summary->addWidget(makeMetric(QStringLiteral("馆藏书目"), &m_reportTitles,
                                  QStringLiteral("blue")));
    summary->addWidget(makeMetric(QStringLiteral("有效读者"), &m_reportReaders,
                                  QStringLiteral("green")));
    summary->addWidget(makeMetric(QStringLiteral("累计借阅"), &m_reportLoans,
                                  QStringLiteral("orange")));
    summary->addWidget(makeMetric(QStringLiteral("当前逾期"), &m_reportOverdue,
                                  QStringLiteral("red")));
    layout->addLayout(summary);

    auto *historyPanel = makePanel();
    auto *historyLayout = new QVBoxLayout(historyPanel);
    historyLayout->setContentsMargins(20, 18, 20, 18);
    auto *historyHeader = new QHBoxLayout;
    historyHeader->addWidget(makeLabel(QStringLiteral("借阅流水"),
                                       QStringLiteral("panelTitle")));
    historyHeader->addStretch();
    auto *exportButton = makePrimaryButton(QStringLiteral("导出 CSV"));
    historyHeader->addWidget(exportButton);
    historyLayout->addLayout(historyHeader);
    m_historyTable = makeTable({
        QStringLiteral("读者编号"), QStringLiteral("读者"),
        QStringLiteral("册编号"), QStringLiteral("馆藏"),
        QStringLiteral("借出日期"), QStringLiteral("应还日期"),
        QStringLiteral("归还日期")
    });
    historyLayout->addWidget(m_historyTable);
    layout->addWidget(historyPanel, 1);

    connect(exportButton, &QPushButton::clicked,
            this, &MainWindow::exportLoans);
    return page;
}

void MainWindow::exportLoans()
{
    const QString fileName = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出借阅流水"),
        QStringLiteral("借阅流水-%1.csv")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (fileName.isEmpty()) {
        return;
    }
    QString error;
    if (!m_statisticsService.exportCsv(m_session, fileName, &error)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), error);
        return;
    }
    showStatus(QStringLiteral("借阅流水已导出"), true);
}

void MainWindow::refreshDashboard()
{
    QString error;
    const DashboardStats stats = m_statisticsService.dashboard(m_session, &error);
    m_titlesStat->setText(QString::number(stats.titles));
    m_availableStat->setText(QString::number(stats.availableCopies));
    m_activeLoansStat->setText(QString::number(stats.activeLoans));
    m_overdueStat->setText(QString::number(stats.overdueLoans));

    const auto loans = m_circulationService.activeLoans(m_session, &error);
    m_dashboardLoans->setRowCount(loans.size());
    for (int row = 0; row < loans.size(); ++row) {
        const auto &loan = loans.at(row);
        m_dashboardLoans->setItem(
            row, 0, new QTableWidgetItem(loan.readerName));
        m_dashboardLoans->setItem(
            row, 1, new QTableWidgetItem(loan.readerNo));
        m_dashboardLoans->setItem(
            row, 2, new QTableWidgetItem(loan.title));
        m_dashboardLoans->setItem(
            row, 3, new QTableWidgetItem(loan.copyCode));
        m_dashboardLoans->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.dueAt)));
        auto *statusItem = new QTableWidgetItem(
            loan.overdue ? QStringLiteral("已逾期") : QStringLiteral("借阅中"));
        if (loan.overdue) {
            statusItem->setForeground(QColor(QStringLiteral("#B43838")));
        }
        m_dashboardLoans->setItem(row, 5, statusItem);
    }
}

void MainWindow::refreshStatistics()
{
    QString error;
    const DashboardStats stats = m_statisticsService.dashboard(m_session, &error);
    const auto history = m_statisticsService.loanHistory(m_session, 500, &error);
    m_reportTitles->setText(QString::number(stats.titles));
    m_reportReaders->setText(QString::number(stats.readers));
    m_reportLoans->setText(QString::number(history.size()));
    m_reportOverdue->setText(QString::number(stats.overdueLoans));

    m_historyTable->setRowCount(history.size());
    for (int row = 0; row < history.size(); ++row) {
        const auto &loan = history.at(row);
        m_historyTable->setItem(
            row, 0, new QTableWidgetItem(loan.readerNo));
        m_historyTable->setItem(
            row, 1, new QTableWidgetItem(loan.readerName));
        m_historyTable->setItem(
            row, 2, new QTableWidgetItem(loan.copyCode));
        m_historyTable->setItem(
            row, 3, new QTableWidgetItem(loan.title));
        m_historyTable->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.borrowedAt)));
        m_historyTable->setItem(
            row, 5, new QTableWidgetItem(shortDate(loan.dueAt)));
        m_historyTable->setItem(
            row, 6, new QTableWidgetItem(
                         loan.returnedAt.isValid()
                             ? shortDate(loan.returnedAt)
                             : QStringLiteral("未归还")));
    }
}
