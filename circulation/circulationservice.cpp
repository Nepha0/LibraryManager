#include "circulationservice.h"

#include "../common/database.h"
#include "../common/operationlogservice.h"
#include "../common/paramservice.h"
#include "../readers/readerservice.h"

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
    if (!PermissionService::requirePermission(session, Permission::ManageCirculation, error)) {
        return false;
    }
    if (readerNo.trimmed().isEmpty() || copyId <= 0) {
        if (error) {
            *error = QStringLiteral("请选择读者和实体册");
        }
        return false;
    }

    ParamService params;
    const auto loanDays =
        params.getIntParam(
            QStringLiteral("borrow_period_days"),
            error);

    if (!loanDays.has_value()) {
        return false;
    }

    if (*loanDays < 1) {
        if (error) {
            *error = QStringLiteral(
                "借阅期限必须为正整数");
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
        "SELECT id FROM readers "
        "WHERE reader_no = ? FOR UPDATE"));
    query.addBindValue(readerNo.trimmed());

    if (!query.exec() || !query.next()) {
        if (error) {
            *error = query.lastError().isValid()
            ? query.lastError().text()
            : QStringLiteral(
                  "未找到该读者编号");
        }
        database.rollback();
        return false;
    }

    const int readerId =
        query.value(0).toInt();

    ReaderService readerService;
    const ReaderEligibilityResult eligibility =
        readerService.checkReaderEligibility(
            session,
            readerId,
            error);

    if (!eligibility.eligible) {
        database.rollback();

        if (error && error->isEmpty()) {
            *error = eligibility.reasons.isEmpty()
            ? QStringLiteral(
                  "该读者当前不能借书")
            : eligibility.reasons.join(
                  QStringLiteral("；"));
        }
        return false;
    }

    query.prepare(QStringLiteral(
        "SELECT status FROM book_copies WHERE id = ? FOR UPDATE"));
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

    query.prepare(QStringLiteral(
        "INSERT INTO loans(copy_id, reader_id, borrowed_at, due_at) "
        "VALUES(?, ?, CURRENT_TIMESTAMP, TIMESTAMPADD(DAY, ?, CURRENT_TIMESTAMP))"));
    query.addBindValue(copyId);
    query.addBindValue(readerId);
    query.addBindValue(*loanDays);
    if (!query.exec()) {
        database.rollback();
        if (error) {
            *error = query.lastError().text();
        }
        return false;
    }

    qlonglong loanId =
        query.lastInsertId().toLongLong();

    if (loanId <= 0) {
        QSqlQuery idQuery(database);
        if (idQuery.exec(QStringLiteral(
                "SELECT LAST_INSERT_ID()"))
            && idQuery.next()) {
            loanId =
                idQuery.value(0).toLongLong();
        }
    }

    if (loanId <= 0) {
        database.rollback();
        if (error) {
            *error = QStringLiteral(
                "无法读取新增借阅记录编号");
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

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("LOAN_BORROW"),
            QStringLiteral("loan"),
            QString::number(loanId),
            QStringLiteral(
                "读者 %1 借阅实体册 %2，借阅期限 %3 天")
                .arg(readerNo.trimmed(),
                     QString::number(copyId),
                     QString::number(*loanDays)),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "借阅失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        database.rollback();
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

bool CirculationService::returnBook(const UserSession &session,
                                    int loanId,
                                    QString *error)
{
    if (!PermissionService::requirePermission(session, Permission::ManageCirculation, error)) {
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
        "SELECT copy_id FROM loans WHERE id = ? AND returned_at IS NULL FOR UPDATE"));
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
        "UPDATE loans SET returned_at = CURRENT_TIMESTAMP "
        "WHERE id = ? AND returned_at IS NULL"));
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

    OperationLogService logService;
    QString logError;

    if (!logService.record(
            session,
            QStringLiteral("LOAN_RETURN"),
            QStringLiteral("loan"),
            QString::number(loanId),
            QStringLiteral(
                "归还借阅记录 %1，对应实体册 %2")
                .arg(QString::number(loanId),
                     QString::number(copyId)),
            &logError)) {
        database.rollback();

        if (error) {
            *error = QStringLiteral(
                "归还失败：无法记录操作日志");
        }
        return false;
    }

    if (!database.commit()) {
        if (error) {
            *error = database.lastError().text();
        }
        database.rollback();
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QVector<ActiveLoanInfo> CirculationService::activeLoans(
    const UserSession &session,
    QString *error) const
{
    QVector<ActiveLoanInfo> result;

    if (!PermissionService::requirePermission(
            session,
            Permission::ManageCirculation,
            error)) {
        return result;
    }
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
        if (error) {
            error->clear();
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
            query.value(5).toDateTime();
        loan.dueAt =
            query.value(6).toDateTime();
        loan.overdue = loan.dueAt < now;
        result.append(loan);
    }
    return result;
}
