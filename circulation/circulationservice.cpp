#include "circulationservice.h"

#include "../common/database.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

bool CirculationService::borrowBook(const UserSession &session,
                                    const QString &readerNo,
                                    int copyId,
                                    QString *error)
{
    if (!session.isAdmin()
        && session.role != QStringLiteral("librarian")) {
        if (error) {
            *error = QStringLiteral("当前账号没有借阅办理权限");
        }
        return false;
    }
    if (readerNo.trimmed().isEmpty() || copyId <= 0) {
        if (error) {
            *error = QStringLiteral("请选择读者和实体册");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT r.id, r.active, rt.max_loans, rt.loan_days, "
        "(SELECT COUNT(*) FROM loans l "
        " WHERE l.reader_id = r.id AND l.returned_at IS NULL) "
        "FROM readers r "
        "JOIN reader_types rt ON rt.id = r.type_id "
        "WHERE r.reader_no = ?"));
    query.addBindValue(readerNo.trimmed());
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("未找到该读者编号");
        }
        return false;
    }
    const int readerId = query.value(0).toInt();
    const bool readerActive = query.value(1).toInt() == 1;
    const int maxLoans = query.value(2).toInt();
    const int loanDays = query.value(3).toInt();
    const int currentLoans = query.value(4).toInt();
    if (!readerActive) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("该读者已停用，不能借书");
        }
        return false;
    }
    if (currentLoans >= maxLoans) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("已达到该读者的借阅上限");
        }
        return false;
    }

    query.prepare(QStringLiteral(
        "SELECT status FROM book_copies WHERE id = ?"));
    query.addBindValue(copyId);
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("未找到该实体册");
        }
        return false;
    }
    if (query.value(0).toString() != QStringLiteral("available")) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("该实体册当前不可借");
        }
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    query.prepare(QStringLiteral(
        "INSERT INTO loans(copy_id, reader_id, borrowed_at, due_at) "
        "VALUES(?, ?, ?, ?)"));
    query.addBindValue(copyId);
    query.addBindValue(readerId);
    query.addBindValue(now.toString(Qt::ISODate));
    query.addBindValue(now.addDays(loanDays).toString(Qt::ISODate));
    if (!query.exec()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    query.prepare(QStringLiteral(
        "UPDATE book_copies SET status = 'borrowed' "
        "WHERE id = ? AND status = 'available'"));
    query.addBindValue(copyId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("实体册状态已变化，请刷新后重试");
        }
        return false;
    }

    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    return true;
}

bool CirculationService::returnBook(const UserSession &session,
                                    int loanId,
                                    QString *error)
{
    if (!session.isAdmin()
        && session.role != QStringLiteral("librarian")) {
        if (error) {
            *error = QStringLiteral("当前账号没有还书办理权限");
        }
        return false;
    }
    if (loanId <= 0) {
        if (error) {
            *error = QStringLiteral("请选择一条借阅记录");
        }
        return false;
    }

    QSqlDatabase database = Database::connection();
    if (!database.transaction()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "SELECT copy_id FROM loans WHERE id = ? AND returned_at IS NULL"));
    query.addBindValue(loanId);
    if (!query.exec() || !query.next()) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("借阅记录不存在或已经归还");
        }
        return false;
    }
    const int copyId = query.value(0).toInt();

    query.prepare(QStringLiteral(
        "UPDATE loans SET returned_at = ? "
        "WHERE id = ? AND returned_at IS NULL"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    query.addBindValue(loanId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("归还失败，请刷新后重试");
        }
        return false;
    }

    query.prepare(QStringLiteral(
        "UPDATE book_copies SET status = 'available' "
        "WHERE id = ? AND status = 'borrowed'"));
    query.addBindValue(copyId);
    if (!query.exec() || query.numRowsAffected() != 1) {
        database.rollback();
        if (error) {
            *error = QStringLiteral("实体册状态异常，归还操作已取消");
        }
        return false;
    }

    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        return false;
    }
    return true;
}

QVector<ActiveLoanInfo> CirculationService::activeLoans(QString *error) const
{
    QVector<ActiveLoanInfo> result;
    QSqlQuery query(Database::connection());
    if (!query.exec(QStringLiteral(
            "SELECT l.id, r.reader_no, r.name, bc.copy_code, b.title, "
            "l.borrowed_at, l.due_at "
            "FROM loans l "
            "JOIN readers r ON r.id = l.reader_id "
            "JOIN book_copies bc ON bc.id = l.copy_id "
            "JOIN books b ON b.id = bc.book_id "
            "WHERE l.returned_at IS NULL "
            "ORDER BY l.due_at, l.id"))) {
        if (error) {
            *error = query.lastError().text();
        }
        return result;
    }
    const QDateTime now = QDateTime::currentDateTime();
    while (query.next()) {
        ActiveLoanInfo loan;
        loan.loanId = query.value(0).toInt();
        loan.readerNo = query.value(1).toString();
        loan.readerName = query.value(2).toString();
        loan.copyCode = query.value(3).toString();
        loan.title = query.value(4).toString();
        loan.borrowedAt =
            QDateTime::fromString(query.value(5).toString(), Qt::ISODate);
        loan.dueAt =
            QDateTime::fromString(query.value(6).toString(), Qt::ISODate);
        loan.overdue = loan.dueAt < now;
        result.append(loan);
    }
    return result;
}
