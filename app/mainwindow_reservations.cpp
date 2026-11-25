#include "mainwindow.h"
#include "windowwidgets.h"
#include "backendintegration.h"
#include "../common/database.h"
#include "../library-backend-a/src/services/CopyService.h"
#include "../library-backend-c/src/services/ReservationService.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace WindowWidgets;

QWidget *MainWindow::buildReservationsPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *actions = new QHBoxLayout;
    auto *refreshButton = new QPushButton(QStringLiteral("刷新"));
    auto *cancelButton = new QPushButton(QStringLiteral("取消所选预约"));
    actions->addStretch();
    actions->addWidget(refreshButton);
    actions->addWidget(cancelButton);
    layout->addLayout(actions);
    m_reservationsTable = makeTable({
        QStringLiteral("书名"), QStringLiteral("读者编号"), QStringLiteral("读者"),
        QStringLiteral("排队号"), QStringLiteral("状态"), QStringLiteral("取书截止时间")
    });
    layout->addWidget(m_reservationsTable);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshReservations);
    connect(cancelButton, &QPushButton::clicked, this, &MainWindow::cancelSelectedReservation);
    return page;
}

void MainWindow::reserveSelectedBook()
{
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    const qint64 bookId = selectedId(m_booksTable);
    if (bookId <= 0) {
        showStatus(QStringLiteral("请先选择要预约的图书"), false);
        return;
    }
    bool accepted = false;
    const QString readerNo = QInputDialog::getText(this, QStringLiteral("预约图书"),
        QStringLiteral("读者编号"), QLineEdit::Normal, {}, &accepted);
    if (!accepted) {
        return;
    }
    QString error;
    const int readerId = findReaderId(readerNo, &error);
    if (readerId == 0) {
        QMessageBox::warning(this, QStringLiteral("无法办理预约"), error);
        return;
    }
    const auto db = Database::connection();
    lib::CopyService copies(db);
    lib::ReservationService service(db, &copies, BackendIntegration::readerEligibility(m_session));
    finishCirculationAction(service.reserve(operatorContext(), readerId, bookId));
}

void MainWindow::cancelSelectedReservation()
{
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    const qint64 reservationId = selectedId(m_reservationsTable);
    if (reservationId <= 0) {
        showStatus(QStringLiteral("请先选择要取消的预约"), false);
        return;
    }
    const auto db = Database::connection();
    lib::CopyService copies(db);
    lib::ReservationService service(db, &copies);
    finishCirculationAction(service.cancelReservation(operatorContext(), reservationId));
}

void MainWindow::refreshReservations()
{
    m_reservationsTable->setRowCount(0);
    if (!checkPermission(Permission::ManageCirculation)) {
        return;
    }
    lib::ReservationService service(Database::connection(), nullptr);
    const auto result = service.listReservations(operatorContext(), {}, 1, 1000);
    if (!result.ok()) {
        showStatus(result.message, false);
        return;
    }
    const QStringList columns{QStringLiteral("title"), QStringLiteral("reader_no"),
        QStringLiteral("reader_name"), QStringLiteral("queue_no"),
        QStringLiteral("status_text"), QStringLiteral("pickup_deadline")};
    m_reservationsTable->setRowCount(result.list.size());
    for (int row = 0; row < result.list.size(); ++row) {
        const auto reservation = result.list.at(row).toMap();
        for (int column = 0; column < columns.size(); ++column) {
            m_reservationsTable->setItem(row, column,
                new QTableWidgetItem(reservation.value(columns.at(column)).toString()));
        }
        m_reservationsTable->item(row, 0)->setData(Qt::UserRole,
            reservation.value(QStringLiteral("reservation_id")));
    }
}
