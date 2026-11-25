#include "service_smoke_test.h"
#include "../app/backendintegration.h"

#include "../books/bookservice.h"
#include "../common/database.h"
#include "../library-backend-a/src/services/BookService.h"
#include "../library-backend-a/src/services/CopyService.h"
#include "../library-backend-c/src/services/BorrowService.h"
#include "../library-backend-c/src/services/RenewService.h"
#include "../library-backend-c/src/services/ReservationService.h"
#include "../library-backend-c/src/services/ReturnService.h"
#include "../statistics/statisticsservice.h"
#include "../library-backend-b/src/readers/readerservice.h"

#include <QDateTime>
#include <QDebug>

bool runServiceSmokeTest()
{
    QString error;
    if (!Database::initializeSmokeTest(&error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto session = AuthService::login(
        QStringLiteral("admin"), QStringLiteral("admin123"), &error);
    if (!session.has_value()) {
        qCritical().noquote() << error;
        return false;
    }

    const QString suffix = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString readerNo = QStringLiteral("SMOKE-%1").arg(suffix);
    const QString readerName = QStringLiteral("冒烟测试读者");
    ReaderService readers;
    if (!readers.addReader(*session, readerNo, readerName, QString(), &error)) {
        qCritical().noquote() << error;
        return false;
    }
    const QString nextReaderNo = readerNo + QStringLiteral("-NEXT");
    if (!readers.addReader(*session, nextReaderNo, readerName, QString(), &error)) {
        qCritical().noquote() << error;
        return false;
    }
    int readerId = 0;
    int nextReaderId = 0;
    for (const auto &reader : readers.readers(*session, {readerNo, {}, {}}, &error)) {
        if (reader.readerNo == readerNo) readerId = reader.id;
        if (reader.readerNo == nextReaderNo) nextReaderId = reader.id;
    }
    if (readerId == 0 || nextReaderId == 0) {
        qCritical().noquote() << "Smoke test could not find its readers." << error;
        return false;
    }

    StatisticsService statistics;
    const DashboardStats before = statistics.dashboard(*session, &error);
    if (!error.isEmpty()) {
        qCritical().noquote() << error;
        return false;
    }

    BookService books;
    const QString isbn =
        QStringLiteral("SMOKE-%1").arg(suffix);
    const QString title = QStringLiteral("冒烟测试图书");
    if (!books.addBook(*session, title,
                       QStringLiteral("测试作者"), isbn,
                       QStringLiteral("计算机"), 1, &error)) {
        qCritical().noquote() << error;
        return false;
    }

    const auto op = BackendIntegration::operatorContext(*session);
    const auto db = Database::connection();
    lib::BookService catalog(db);
    const auto found = catalog.listBooks(op, {{QStringLiteral("keyword"), suffix.right(10)}});
    if (!found.ok() || found.list.size() != 1) {
        qCritical().noquote() << "Smoke test could not search the new book." << found.message;
        return false;
    }
    const qint64 bookId = found.list.front().toMap().value(QStringLiteral("book_id")).toLongLong();
    lib::CopyService copies(db);
    const auto bookCopies = copies.listCopiesByBook(op, bookId);
    if (!bookCopies.ok() || bookCopies.list.size() != 1) {
        qCritical().noquote() << "Smoke test could not find the new copy." << bookCopies.message;
        return false;
    }
    const QString barcode = bookCopies.list.front().toMap().value(QStringLiteral("barcode")).toString();
    const auto eligibility = BackendIntegration::readerEligibility(*session);
    lib::BorrowService borrowing(db, &copies, eligibility);
    const auto borrowAudit = BackendIntegration::loanAudit(*session, QStringLiteral("LOAN_BORROW"), barcode);
    const auto loan = borrowing.borrow(op, readerId, barcode, borrowAudit);
    if (!loan.ok()) {
        qCritical().noquote() << "Borrow failed:" << loan.message;
        return false;
    }
    const qint64 loanId = loan.data.value(QStringLiteral("loan_id")).toLongLong();
    const DashboardStats during = statistics.dashboard(*session, &error);
    if (!error.isEmpty() || during.activeLoans != before.activeLoans + 1
        || during.availableCopies != before.availableCopies) {
        qCritical().noquote() << "Borrow did not update statistics as expected." << error;
        return false;
    }
    const auto duplicate = borrowing.borrow(op, nextReaderId, barcode);
    if (duplicate.code != lib::toErrorCode(lib::ErrorCodeC::CopyNotAvailable)) {
        qCritical().noquote() << "A second borrow was not rejected as expected." << duplicate.message;
        return false;
    }
    lib::RenewService renewals(db, eligibility);
    const auto renewed = renewals.renew(op, loanId);
    const QString dueAt = loan.data.value(QStringLiteral("due_at")).toString();
    if (!renewed.ok() || renewed.data.value(QStringLiteral("new_due_at")).toString() <= dueAt) {
        qCritical().noquote() << "Renew failed:" << renewed.message;
        return false;
    }
    lib::ReservationService reservations(db, &copies, eligibility);
    const auto reserved = reservations.reserve(op, nextReaderId, bookId);
    const qint64 reservationId = reserved.data.value(QStringLiteral("reservation_id")).toLongLong();
    if (!reserved.ok() || reservationId <= 0) {
        qCritical().noquote() << "Reserve failed:" << reserved.message;
        return false;
    }
    lib::ReturnService returns(db, &copies);
    const auto returnAudit = BackendIntegration::loanAudit(*session, QStringLiteral("LOAN_RETURN"), barcode);
    const auto returned = returns.returnBook(op, {{QStringLiteral("loan_id"), loanId}}, returnAudit);
    if (!returned.ok()) {
        qCritical().noquote() << "Return failed:" << returned.message;
        return false;
    }
    const auto queue = reservations.listReservations(op, {{QStringLiteral("book_id"), bookId}});
    if (!queue.ok() || queue.list.size() != 1 ||
        queue.list.front().toMap().value(QStringLiteral("status")) != QStringLiteral("ready_to_pick")) {
        qCritical().noquote() << "Return did not make the reservation ready." << queue.message;
        return false;
    }
    const auto cancelled = reservations.cancelReservation(op, reservationId);
    if (!cancelled.ok()) {
        qCritical().noquote() << "Cancel failed:" << cancelled.message;
        return false;
    }

    const auto history = statistics.loanHistory(*session, 1, &error);
    if (history.isEmpty() || history.front().readerNo != readerNo
        || !history.front().returnedAt.isValid()) {
        qCritical().noquote() << "Returned loan is missing from history." << error;
        return false;
    }
    const DashboardStats stats = statistics.dashboard(*session, &error);
    if (!error.isEmpty() || stats.titles != before.titles + 1
        || stats.availableCopies != before.availableCopies + 1
        || stats.activeLoans != before.activeLoans) {
        qCritical().noquote()
            << (error.isEmpty() ? QStringLiteral("统计数据异常") : error);
        return false;
    }
    qInfo() << "Smoke test passed: login, readers, catalog search, borrow, reject duplicate, renew, reserve, return, cancel, statistics.";
    return true;
}
