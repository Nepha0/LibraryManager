#include "mainwindow.h"
#include "windowwidgets.h"

#include <QColor>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace WindowWidgets;

QWidget *MainWindow::buildReadersPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *formPanel = makePanel();
    auto *formLayout = new QVBoxLayout(formPanel);
    formLayout->setContentsMargins(20, 18, 20, 18);

    formLayout->addWidget(
        makeLabel(
            QStringLiteral("新增或编辑读者"),
            QStringLiteral("panelTitle")));

    auto *form = new QHBoxLayout;

    m_readerNo = new QLineEdit;
    m_readerNo->setPlaceholderText(
        QStringLiteral("读者编号"));

    m_readerName = new QLineEdit;
    m_readerName->setPlaceholderText(
        QStringLiteral("姓名"));

    m_readerPhone = new QLineEdit;
    m_readerPhone->setPlaceholderText(
        QStringLiteral("手机号（可选）"));

    auto *addButton =
        makePrimaryButton(
            QStringLiteral("登记读者"));
    auto *updateButton =
        new QPushButton(
            QStringLiteral("保存修改"));
    auto *statusButton =
        new QPushButton(
            QStringLiteral("切换启停"));
    auto *clearButton =
        new QPushButton(
            QStringLiteral("清空选择"));

    form->addWidget(m_readerNo, 2);
    form->addWidget(m_readerName, 2);
    form->addWidget(m_readerPhone, 2);
    form->addWidget(addButton);
    form->addWidget(updateButton);
    form->addWidget(statusButton);
    form->addWidget(clearButton);

    formLayout->addLayout(form);

    m_readerDetail = makeLabel(
        QStringLiteral(
            "选择读者后显示详情与借阅概况"));
    m_readerDetail->setWordWrap(true);
    formLayout->addWidget(m_readerDetail);

    layout->addWidget(formPanel);

    auto *filterPanel = makePanel();
    auto *filterLayout =
        new QHBoxLayout(filterPanel);
    filterLayout->setContentsMargins(
        20, 14, 20, 14);

    m_readerFilterNo = new QLineEdit;
    m_readerFilterNo->setPlaceholderText(
        QStringLiteral("按读者编号筛选"));

    m_readerFilterName = new QLineEdit;
    m_readerFilterName->setPlaceholderText(
        QStringLiteral("按姓名筛选"));

    m_readerFilterStatus = new QComboBox;
    m_readerFilterStatus->addItems({
        QStringLiteral("全部状态"),
        QStringLiteral("正常"),
        QStringLiteral("停用")
    });

    auto *filterButton =
        new QPushButton(
            QStringLiteral("筛选"));
    auto *resetFilterButton =
        new QPushButton(
            QStringLiteral("重置筛选"));

    filterLayout->addWidget(m_readerFilterNo, 2);
    filterLayout->addWidget(m_readerFilterName, 2);
    filterLayout->addWidget(
        m_readerFilterStatus, 1);
    filterLayout->addWidget(filterButton);
    filterLayout->addWidget(resetFilterButton);

    layout->addWidget(filterPanel);

    auto *readersPanel = makePanel();
    auto *readersLayout =
        new QVBoxLayout(readersPanel);
    readersLayout->setContentsMargins(
        20, 18, 20, 18);

    readersLayout->addWidget(
        makeLabel(
            QStringLiteral("读者名册"),
            QStringLiteral("panelTitle")));

    m_readersTable = makeTable({
        QStringLiteral("读者编号"),
        QStringLiteral("姓名"),
        QStringLiteral("联系电话"),
        QStringLiteral("借阅中"),
        QStringLiteral("状态")
    });

    readersLayout->addWidget(m_readersTable);
    layout->addWidget(readersPanel, 1);

    connect(
        addButton,
        &QPushButton::clicked,
        this,
        &MainWindow::addReader);

    connect(
        updateButton,
        &QPushButton::clicked,
        this,
        &MainWindow::updateSelectedReader);

    connect(
        statusButton,
        &QPushButton::clicked,
        this,
        &MainWindow::toggleSelectedReaderStatus);

    connect(
        clearButton,
        &QPushButton::clicked,
        this,
        &MainWindow::clearReaderSelection);

    connect(
        filterButton,
        &QPushButton::clicked,
        this,
        &MainWindow::refreshReaders);

    connect(
        m_readerFilterNo,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::refreshReaders);

    connect(
        m_readerFilterName,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::refreshReaders);

    connect(
        m_readerFilterStatus,
        &QComboBox::currentIndexChanged,
        this,
        [this](int) {
            refreshReaders();
        });

    connect(
        resetFilterButton,
        &QPushButton::clicked,
        this,
        [this] {
            m_readerFilterNo->clear();
            m_readerFilterName->clear();
            m_readerFilterStatus->setCurrentIndex(0);
            refreshReaders();
        });

    connect(
        m_readersTable,
        &QTableWidget::itemSelectionChanged,
        this,
        &MainWindow::showSelectedReaderDetail);

    connect(
        m_readerPhone,
        &QLineEdit::returnPressed,
        this,
        &MainWindow::addReader);

    return page;
}

void MainWindow::addReader()
{
    QString error;
    if (!m_readerService.addReader(
            m_session, m_readerNo->text(), m_readerName->text(),
            m_readerPhone->text(), &error)) {
        QMessageBox::warning(this, QStringLiteral("无法登记读者"), error);
        return;
    }
    const QString name = m_readerName->text().trimmed();
    clearReaderSelection();
    refreshAll();
    showStatus(QStringLiteral("读者“%1”已登记").arg(name), true);
}

void MainWindow::clearReaderSelection()
{
    m_selectedReaderId = 0;
    m_selectedReaderEnabled = true;

    if (m_readersTable) {
        m_readersTable->clearSelection();
        m_readersTable->setCurrentCell(-1, -1);
    }

    if (m_readerNo) {
        m_readerNo->clear();
    }

    if (m_readerName) {
        m_readerName->clear();
    }

    if (m_readerPhone) {
        m_readerPhone->clear();
    }

    if (m_readerDetail) {
        m_readerDetail->setText(
            QStringLiteral(
                "选择读者后显示详情与借阅概况"));
    }

    if (m_readerNo) {
        m_readerNo->setFocus();
    }
}

void MainWindow::showSelectedReaderDetail()
{
    const int row = m_readersTable->currentRow();

    if (row < 0) {
        m_selectedReaderId = 0;
        m_selectedReaderEnabled = true;

        if (m_readerDetail) {
            m_readerDetail->setText(
                QStringLiteral(
                    "选择读者后显示详情与借阅概况"));
        }
        return;
    }

    QTableWidgetItem *readerItem =
        m_readersTable->item(row, 0);

    if (!readerItem) {
        return;
    }

    const int readerId =
        readerItem->data(Qt::UserRole).toInt();

    if (readerId <= 0) {
        return;
    }

    QString error;
    const auto detail =
        m_readerService.getReader(
            m_session, readerId, &error);

    if (!detail.has_value()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法读取读者详情"),
            error);
        return;
    }

    m_selectedReaderId = detail->info.id;
    m_selectedReaderEnabled =
        detail->info.enabled;

    m_readerNo->setText(
        detail->info.readerNo);
    m_readerName->setText(
        detail->info.name);
    m_readerPhone->setText(
        detail->info.phone);

    m_readerDetail->setText(
        QStringLiteral(
            "状态：%1　当前借阅：%2　"
            "累计借阅：%3　已归还：%4　"
            "当前逾期：%5")
            .arg(
                detail->info.enabled
                    ? QStringLiteral("正常")
                    : QStringLiteral("停用"))
            .arg(detail->info.activeLoans)
            .arg(detail->totalLoans)
            .arg(detail->returnedLoans)
            .arg(detail->overdueLoans));
}

void MainWindow::updateSelectedReader()
{
    if (m_selectedReaderId <= 0) {
        QMessageBox::information(
            this,
            QStringLiteral("修改读者"),
            QStringLiteral(
                "请先在读者名册中选择一名读者"));
        return;
    }

    const QString readerName =
        m_readerName->text().trimmed();

    QString error;
    if (!m_readerService.updateReader(
            m_session,
            m_selectedReaderId,
            m_readerNo->text(),
            m_readerName->text(),
            m_readerPhone->text(),
            &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法修改读者"),
            error);
        return;
    }

    clearReaderSelection();
    refreshReaders();

    showStatus(
        QStringLiteral("读者“%1”的资料已更新")
            .arg(readerName),
        true);
}

void MainWindow::toggleSelectedReaderStatus()
{
    if (m_selectedReaderId <= 0) {
        QMessageBox::information(
            this,
            QStringLiteral("修改读者状态"),
            QStringLiteral(
                "请先在读者名册中选择一名读者"));
        return;
    }

    const bool targetEnabled =
        !m_selectedReaderEnabled;

    const QString action =
        targetEnabled
            ? QStringLiteral("启用")
            : QStringLiteral("停用");

    const auto answer =
        QMessageBox::question(
            this,
            QStringLiteral("确认修改状态"),
            QStringLiteral(
                "确定要%1当前选择的读者吗？")
                .arg(action),
            QMessageBox::Yes
                | QMessageBox::No,
            QMessageBox::No);

    if (answer != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_readerService.setReaderStatus(
            m_session,
            m_selectedReaderId,
            targetEnabled,
            &error)) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法修改读者状态"),
            error);
        return;
    }

    clearReaderSelection();
    refreshReaders();

    showStatus(
        QStringLiteral("读者状态已修改为“%1”")
            .arg(
                targetEnabled
                    ? QStringLiteral("正常")
                    : QStringLiteral("停用")),
        true);
}

void MainWindow::refreshReaders()
{
    ReaderQuery query;

    if (m_readerFilterNo) {
        query.readerNo =
            m_readerFilterNo->text().trimmed();
    }

    if (m_readerFilterName) {
        query.name =
            m_readerFilterName->text().trimmed();
    }

    if (m_readerFilterStatus) {
        if (m_readerFilterStatus->currentIndex() == 1) {
            query.enabled = true;
        } else if (m_readerFilterStatus->currentIndex() == 2) {
            query.enabled = false;
        }
    }

    QString error;
    const auto readers =
        m_readerService.readers(
            m_session, query, &error);

    if (!error.isEmpty()) {
        m_readersTable->setRowCount(0);
        showStatus(error, false);
        return;
    }

    m_selectedReaderId = 0;
    m_selectedReaderEnabled = true;

    if (m_readerDetail) {
        m_readerDetail->setText(
            QStringLiteral(
                "选择读者后显示详情与借阅概况"));
    }

    m_readersTable->setRowCount(readers.size());

    for (int row = 0; row < readers.size(); ++row) {
        const auto &reader = readers.at(row);

        auto *readerNoItem =
            new QTableWidgetItem(reader.readerNo);

        // 后续选中、修改和启停时通过这里取得数据库 ID。
        readerNoItem->setData(
            Qt::UserRole, reader.id);

        m_readersTable->setItem(
            row, 0, readerNoItem);

        m_readersTable->setItem(
            row, 1,
            new QTableWidgetItem(reader.name));

        m_readersTable->setItem(
            row, 2,
            new QTableWidgetItem(reader.phone));

        m_readersTable->setItem(
            row, 3,
            new QTableWidgetItem(
                QString::number(reader.activeLoans)));

        auto *statusItem =
            new QTableWidgetItem(
                reader.enabled
                    ? QStringLiteral("正常")
                    : QStringLiteral("停用"));

        if (!reader.enabled) {
            statusItem->setForeground(
                QColor(QStringLiteral("#B43838")));
        }

        m_readersTable->setItem(
            row, 4, statusItem);
    }
}
