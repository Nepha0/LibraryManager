#include "mainwindow.h"
#include "windowwidgets.h"
#include "backendintegration.h"
#include "../common/database.h"
#include "../library-backend-a/src/services/CopyService.h"
#include "../library-backend-c/src/services/BorrowService.h"
#include "../library-backend-c/src/services/RenewService.h"
#include "../library-backend-c/src/services/ReturnService.h"

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

QWidget *MainWindow::buildCirculationPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    auto *borrowPanel = makePanel();
    auto *borrowLayout = new QVBoxLayout(borrowPanel);
    borrowLayout->setContentsMargins(20, 18, 20, 18);
    auto *borrowTitle = new QHBoxLayout;
    borrowTitle->addWidget(makeLabel(QStringLiteral("办理借阅"),
                                     QStringLiteral("panelTitle")));
    borrowTitle->addStretch();
    borrowTitle->addWidget(makeLabel(
        QStringLiteral("借阅期限和上限以系统参数为准"),
        QStringLiteral("mutedText")));
    borrowLayout->addLayout(borrowTitle);
    auto *borrowRow = new QHBoxLayout;
    m_borrowReaderNo = new QLineEdit;
    m_borrowReaderNo->setPlaceholderText(QStringLiteral("读者编号，例如 R001"));
    m_borrowCopy = new QComboBox;
    m_borrowCopy->setEditable(true);
    m_borrowCopy->setInsertPolicy(QComboBox::NoInsert);
    m_borrowCopy->lineEdit()->setPlaceholderText(QStringLiteral("选择副本或输入条码"));
    m_borrowCopy->setMinimumWidth(290);
    auto *borrowButton = makePrimaryButton(QStringLiteral("确认借出"));
    borrowRow->addWidget(m_borrowReaderNo, 2);
    borrowRow->addWidget(m_borrowCopy, 4);
    borrowRow->addWidget(borrowButton);
    borrowLayout->addLayout(borrowRow);
    layout->addWidget(borrowPanel);

    auto *loansPanel = makePanel();
    auto *loansLayout = new QVBoxLayout(loansPanel);
    loansLayout->setContentsMargins(20, 18, 20, 18);
    auto *loansHeader = new QHBoxLayout;
    loansHeader->addWidget(makeLabel(QStringLiteral("当前借阅"),
                                     QStringLiteral("panelTitle")));
    loansHeader->addStretch();
    auto *renewButton = new QPushButton(QStringLiteral("续借所选"));
    loansHeader->addWidget(renewButton);
    auto *returnButton = new QPushButton(QStringLiteral("归还所选"));
    returnButton->setProperty("role", QStringLiteral("danger"));
    loansHeader->addWidget(returnButton);
    loansLayout->addLayout(loansHeader);
    m_activeLoansTable = makeTable({
        QStringLiteral("读者"), QStringLiteral("读者编号"),
        QStringLiteral("馆藏"), QStringLiteral("册编号"),
        QStringLiteral("借出日期"), QStringLiteral("应还日期"),
        QStringLiteral("状态")
    });
    loansLayout->addWidget(m_activeLoansTable);
    layout->addWidget(loansPanel, 1);

    connect(borrowButton, &QPushButton::clicked,
            this, &MainWindow::borrowBook);
    connect(m_borrowReaderNo, &QLineEdit::returnPressed,
            this, &MainWindow::borrowBook);
    connect(returnButton, &QPushButton::clicked,
            this, &MainWindow::returnSelectedBook);
    connect(renewButton, &QPushButton::clicked, this, &MainWindow::renewSelectedLoan);
    return page;
}

void MainWindow::borrowBook()
{
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    QString error;
    const int readerId = findReaderId(m_borrowReaderNo->text(), &error);
    if (readerId == 0) {
        QMessageBox::warning(this, QStringLiteral("无法办理借阅"), error);
        return;
    }

    const QString text = m_borrowCopy->currentText().trimmed();
    const int index = m_borrowCopy->currentIndex();
    const QString barcode = index >= 0 && text == m_borrowCopy->itemText(index)
                                ? m_borrowCopy->itemData(index).toString() : text;
    const auto db = Database::connection();
    lib::CopyService copies(db);
    lib::BorrowService service(db, &copies, BackendIntegration::readerEligibility(m_session));
    const auto audit = BackendIntegration::loanAudit(m_session, QStringLiteral("LOAN_BORROW"),
        QStringLiteral("读者 %1 借阅副本 %2").arg(m_borrowReaderNo->text().trimmed(), barcode));
    const auto result = service.borrow(operatorContext(), readerId, barcode, audit);
    if (result.ok()) {
        m_borrowReaderNo->clear();
        m_borrowCopy->setCurrentIndex(-1);
        m_borrowCopy->clearEditText();
    }
    finishCirculationAction(result);
}

void MainWindow::returnSelectedBook()
{
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    const qint64 loanId = selectedId(m_activeLoansTable);
    if (loanId <= 0) {
        showStatus(QStringLiteral("请先选择要归还的借阅记录"), false);
        return;
    }
    const auto db = Database::connection();
    lib::CopyService copies(db);
    lib::ReturnService service(db, &copies);
    const auto audit = BackendIntegration::loanAudit(m_session, QStringLiteral("LOAN_RETURN"),
        QStringLiteral("归还借阅记录 %1").arg(loanId));
    finishCirculationAction(service.returnBook(operatorContext(),
                            {{QStringLiteral("loan_id"), loanId}}, audit));
}

void MainWindow::renewSelectedLoan()
{
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    const qint64 loanId = selectedId(m_activeLoansTable);
    if (loanId <= 0) {
        showStatus(QStringLiteral("请先选择要续借的借阅记录"), false);
        return;
    }
    lib::RenewService service(Database::connection(), BackendIntegration::readerEligibility(m_session));
    finishCirculationAction(service.renew(operatorContext(), loanId));
}

void MainWindow::finishCirculationAction(const lib::Result &result)
{
    if (!result.ok()) {
        QMessageBox::warning(this, QStringLiteral("办理失败"), result.message);
        return;
    }
    refreshAll();
    showStatus(result.message, true);
}

lib::OperatorContext MainWindow::operatorContext() const
{
    return BackendIntegration::operatorContext(m_session);
}

int MainWindow::findReaderId(const QString &readerNo, QString *error) const
{
    const QString normalizedReaderNo = readerNo.trimmed();
    if (normalizedReaderNo.isEmpty()) {
        *error = QStringLiteral("请填写读者编号");
        return 0;
    }
    QVariantMap filter;
    filter.insert(QStringLiteral("reader_no"), normalizedReaderNo);
    filter.insert(QStringLiteral("exact_reader_no"), true);
    const lib::Result result = m_readerService.listReaders(
        operatorContext(), filter, 1, 1);
    if (!result.ok()) {
        *error = result.message;
        return 0;
    }
    for (const QVariant &value : result.list) {
        const QVariantMap reader = value.toMap();
        if (reader.value(QStringLiteral("reader_no")).toString()
                .compare(normalizedReaderNo, Qt::CaseInsensitive) == 0) {
            return reader.value(QStringLiteral("reader_id")).toInt();
        }
    }
    *error = QStringLiteral("未找到该读者编号");
    return 0;
}

void MainWindow::refreshCopies()
{
    QString error;
    m_borrowCopy->clear();
    const auto copies = m_bookService.availableCopies(m_session, &error);
    for (const auto &copy : copies) {
        m_borrowCopy->addItem(
            QStringLiteral("%1  ·  %2 / %3")
                .arg(copy.code, copy.title, copy.author),
            copy.code);
    }
    m_borrowCopy->setCurrentIndex(-1);
}

void MainWindow::refreshCirculation()
{
    QString error;
    const auto loans = m_circulationService.activeLoans(m_session, &error);
    m_activeLoansTable->setRowCount(loans.size());
    for (int row = 0; row < loans.size(); ++row) {
        const auto &loan = loans.at(row);
        auto *readerItem = new QTableWidgetItem(loan.readerName);
        readerItem->setData(Qt::UserRole, loan.loanId);
        m_activeLoansTable->setItem(row, 0, readerItem);
        m_activeLoansTable->setItem(
            row, 1, new QTableWidgetItem(loan.readerNo));
        m_activeLoansTable->setItem(
            row, 2, new QTableWidgetItem(loan.title));
        m_activeLoansTable->setItem(
            row, 3, new QTableWidgetItem(loan.copyCode));
        m_activeLoansTable->setItem(
            row, 4, new QTableWidgetItem(shortDate(loan.borrowedAt)));
        m_activeLoansTable->setItem(
            row, 5, new QTableWidgetItem(shortDate(loan.dueAt)));
        auto *statusItem = new QTableWidgetItem(
            loan.overdue ? QStringLiteral("已逾期") : QStringLiteral("借阅中"));
        if (loan.overdue) {
            statusItem->setForeground(QColor(QStringLiteral("#B43838")));
        }
        m_activeLoansTable->setItem(row, 6, statusItem);
    }
}
