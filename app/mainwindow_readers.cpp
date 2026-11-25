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
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace WindowWidgets;

namespace {
constexpr int ReaderPageSize = 50;
}

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

    auto *pagination = new QHBoxLayout;
    m_readerPreviousPage = new QPushButton(QStringLiteral("上一页"));
    m_readerNextPage = new QPushButton(QStringLiteral("下一页"));
    m_readerPageLabel = makeLabel(QString());
    m_readerPreviousPage->setEnabled(false);
    m_readerNextPage->setEnabled(false);
    pagination->addStretch();
    pagination->addWidget(m_readerPreviousPage);
    pagination->addWidget(m_readerPageLabel);
    pagination->addWidget(m_readerNextPage);
    readersLayout->addLayout(pagination);
    layout->addWidget(readersPanel, 1);

    connect(m_readerPreviousPage, &QPushButton::clicked, this, [this] {
        --m_readerPage;
        refreshReaders();
    });
    connect(m_readerNextPage, &QPushButton::clicked, this, [this] {
        ++m_readerPage;
        refreshReaders();
    });

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
            const QSignalBlocker blocker(m_readerFilterStatus);
            m_readerFilterStatus->setCurrentIndex(0);
            m_readerPage = 1;
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
    QVariantMap fields;
    fields.insert(QStringLiteral("reader_no"), m_readerNo->text());
    fields.insert(QStringLiteral("name"), m_readerName->text());
    fields.insert(QStringLiteral("phone"), m_readerPhone->text());
    const lib::Result result = m_readerService.addReader(
        operatorContext(), fields);
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("无法登记读者"),
                             result.message);
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

    const lib::Result result = m_readerService.getReader(
        operatorContext(), readerId);
    if (!result.ok()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法读取读者详情"),
            result.message);
        return;
    }

    const QVariantMap detail = result.data;
    m_selectedReaderId = detail.value(QStringLiteral("reader_id")).toInt();
    m_selectedReaderEnabled = detail.value(QStringLiteral("enabled")).toBool();

    m_readerNo->setText(
        detail.value(QStringLiteral("reader_no")).toString());
    m_readerName->setText(
        detail.value(QStringLiteral("name")).toString());
    m_readerPhone->setText(
        detail.value(QStringLiteral("phone")).toString());

    m_readerDetail->setText(
        QStringLiteral(
            "状态：%1　当前借阅：%2　"
            "累计借阅：%3　已归还：%4　"
            "当前逾期：%5")
            .arg(
                detail.value(QStringLiteral("enabled")).toBool()
                    ? QStringLiteral("正常")
                    : QStringLiteral("停用"))
            .arg(detail.value(QStringLiteral("active_loans")).toInt())
            .arg(detail.value(QStringLiteral("total_loans")).toInt())
            .arg(detail.value(QStringLiteral("returned_loans")).toInt())
            .arg(detail.value(QStringLiteral("overdue_loans")).toInt()));
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

    QVariantMap fields;
    fields.insert(QStringLiteral("reader_no"), m_readerNo->text());
    fields.insert(QStringLiteral("name"), m_readerName->text());
    fields.insert(QStringLiteral("phone"), m_readerPhone->text());
    const lib::Result result = m_readerService.updateReader(
        operatorContext(), m_selectedReaderId, fields);
    if (!result.ok()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法修改读者"),
            result.message);
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

    const lib::Result result = m_readerService.setReaderStatus(
        operatorContext(), m_selectedReaderId, targetEnabled);
    if (!result.ok()) {
        QMessageBox::warning(
            this,
            QStringLiteral("无法修改读者状态"),
            result.message);
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
    QVariantMap filter;

    if (m_readerFilterNo) {
        filter.insert(QStringLiteral("reader_no"),
                      m_readerFilterNo->text().trimmed());
    }

    if (m_readerFilterName) {
        filter.insert(QStringLiteral("name"),
                      m_readerFilterName->text().trimmed());
    }

    if (m_readerFilterStatus) {
        if (m_readerFilterStatus->currentIndex() == 1) {
            filter.insert(QStringLiteral("enabled"), true);
        } else if (m_readerFilterStatus->currentIndex() == 2) {
            filter.insert(QStringLiteral("enabled"), false);
        }
    }

    if (filter != m_appliedReaderFilter) {
        m_appliedReaderFilter = filter;
        m_readerPage = 1;
    }

    const auto pageCount = [](int total) {
        return total > 0 ? (total - 1) / ReaderPageSize + 1 : 1;
    };
    lib::Result result = m_readerService.listReaders(
        operatorContext(), filter, m_readerPage, ReaderPageSize);
    if (result.ok() && m_readerPage > pageCount(result.total)) {
        m_readerPage = pageCount(result.total);
        result = m_readerService.listReaders(
            operatorContext(), filter, m_readerPage, ReaderPageSize);
    }

    const QSignalBlocker selectionBlocker(m_readersTable);
    m_readersTable->clearSelection();
    m_readersTable->setCurrentCell(-1, -1);
    m_selectedReaderId = 0;
    m_selectedReaderEnabled = true;
    m_readerDetail->setText(QStringLiteral("选择读者后显示详情与借阅概况"));

    if (!result.ok()) {
        m_readerPreviousPage->setEnabled(false);
        m_readerNextPage->setEnabled(false);
        m_readerPageLabel->clear();
        m_readersTable->setRowCount(0);
        showStatus(result.message, false);
        return;
    }

    const int pages = pageCount(result.total);
    m_readerPreviousPage->setEnabled(m_readerPage > 1);
    m_readerNextPage->setEnabled(m_readerPage < pages);
    m_readerPageLabel->setText(QStringLiteral("第 %1 / %2 页 · 共 %3 条")
                                   .arg(m_readerPage).arg(pages).arg(result.total));

    const QVariantList readers = result.list;
    m_readersTable->setRowCount(readers.size());

    for (int row = 0; row < readers.size(); ++row) {
        const QVariantMap reader = readers.at(row).toMap();

        auto *readerNoItem =
            new QTableWidgetItem(
                reader.value(QStringLiteral("reader_no")).toString());

        // 后续选中、修改和启停时通过这里取得数据库 ID。
        readerNoItem->setData(
            Qt::UserRole,
            reader.value(QStringLiteral("reader_id")).toLongLong());

        m_readersTable->setItem(
            row, 0, readerNoItem);

        m_readersTable->setItem(
            row, 1,
            new QTableWidgetItem(
                reader.value(QStringLiteral("name")).toString()));

        m_readersTable->setItem(
            row, 2,
            new QTableWidgetItem(
                reader.value(QStringLiteral("phone")).toString()));

        m_readersTable->setItem(
            row, 3,
            new QTableWidgetItem(
                QString::number(
                    reader.value(QStringLiteral("active_loans")).toInt())));

        auto *statusItem =
            new QTableWidgetItem(
                reader.value(QStringLiteral("enabled")).toBool()
                    ? QStringLiteral("正常")
                    : QStringLiteral("停用"));

        if (!reader.value(QStringLiteral("enabled")).toBool()) {
            statusItem->setForeground(
                QColor(QStringLiteral("#B43838")));
        }

        m_readersTable->setItem(
            row, 4, statusItem);
    }
}
