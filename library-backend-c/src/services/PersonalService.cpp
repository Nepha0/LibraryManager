#include "PersonalService.h"

#include <QSqlError>

namespace lib {

PersonalService::PersonalService(QSqlDatabase db)
    : m_db(db) {}

// ===================== 个人中心聚合 =====================
Result PersonalService::getPersonalOverview(const OperatorContext& op, qint64 readerId)
{
    // 1) 操作者校验：Reader 只能查本人（业务层强制，非仅前端隐藏）
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (readerId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少读者"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT reader_no, name, phone, active, registered_at, user_id "
        "FROM readers WHERE id = ?"));
    q.addBindValue(readerId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询读者失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return Result::fail(ErrorCode::ForeignKeyNotFound, QStringLiteral("读者不存在"));
    const QVariant readerUserId = q.value(5);
    if (op.role == Role::Reader
        && (!readerUserId.isValid() || readerUserId.toLongLong() != op.userId))
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅可查看本人个人中心"));

    Result r = Result::success();
    QVariantMap profile;
    profile.insert(QStringLiteral("reader_id"), readerId);
    profile.insert(QStringLiteral("reader_no"), q.value(0).toString());
    profile.insert(QStringLiteral("name"), q.value(1).toString());
    profile.insert(QStringLiteral("phone"), q.value(2).toString());
    profile.insert(QStringLiteral("status"), q.value(3).toInt() == 1 ? QStringLiteral("正常") : QStringLiteral("已停用"));
    profile.insert(QStringLiteral("registered_at"), readDateTimeValue(q.value(4)).toString(Qt::ISODate));
    r.data.insert(QStringLiteral("profile"), profile);

    const QDate today = QDate::currentDate();

    // 2) 当前借阅（在借，含应还日期与逾期天数）
    q.prepare(QStringLiteral(
        "SELECT l.id, l.book_id, b.title, bc.copy_code AS barcode, l.borrowed_at, l.due_at, l.renew_count "
        "FROM loans l JOIN books b ON b.id = l.book_id JOIN book_copies bc ON bc.id = l.copy_id "
        "WHERE l.reader_id = ? AND l.returned_at IS NULL ORDER BY l.due_at ASC"));
    q.addBindValue(readerId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询当前借阅失败: %1").arg(q.lastError().text()));
    QVariantList activeLoans;
    while (q.next()) {
        const QDate dueAt = readDateValue(q.value(5));
        const int overdueDays = (dueAt.isValid() && dueAt < today) ? dueAt.daysTo(today) : 0;
        QVariantMap row;
        row.insert(QStringLiteral("loan_id"), q.value(0).toLongLong());
        row.insert(QStringLiteral("book_id"), q.value(1).toLongLong());
        row.insert(QStringLiteral("title"), q.value(2).toString());
        row.insert(QStringLiteral("barcode"), q.value(3).toString());
        row.insert(QStringLiteral("borrowed_at"), readDateValue(q.value(4)).toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("due_at"), dueAt.toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("renew_count"), q.value(6).toInt());
        row.insert(QStringLiteral("overdue_days"), overdueDays);
        activeLoans.append(row);
    }
    r.data.insert(QStringLiteral("active_loans"), activeLoans);

    // 3) 历史借阅
    q.prepare(QStringLiteral(
        "SELECT l.id, l.book_id, b.title, bc.copy_code AS barcode, l.borrowed_at, l.due_at, l.returned_at, l.renew_count "
        "FROM loans l JOIN books b ON b.id = l.book_id JOIN book_copies bc ON bc.id = l.copy_id "
        "WHERE l.reader_id = ? AND l.returned_at IS NOT NULL ORDER BY l.returned_at DESC LIMIT 100"));
    q.addBindValue(readerId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询历史借阅失败: %1").arg(q.lastError().text()));
    QVariantList history;
    while (q.next()) {
        const QDate dueAt = readDateValue(q.value(5));
        const QDate returnedAt = readDateValue(q.value(6));
        const int overdueDays = (dueAt.isValid() && returnedAt.isValid() && returnedAt > dueAt)
                                    ? dueAt.daysTo(returnedAt) : 0;
        QVariantMap row;
        row.insert(QStringLiteral("loan_id"), q.value(0).toLongLong());
        row.insert(QStringLiteral("book_id"), q.value(1).toLongLong());
        row.insert(QStringLiteral("title"), q.value(2).toString());
        row.insert(QStringLiteral("barcode"), q.value(3).toString());
        row.insert(QStringLiteral("borrowed_at"), readDateValue(q.value(4)).toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("due_at"), dueAt.toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("returned_at"), returnedAt.toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("renew_count"), q.value(7).toInt());
        row.insert(QStringLiteral("overdue_days"), overdueDays);
        history.append(row);
    }
    r.data.insert(QStringLiteral("history"), history);

    // 4) 预约记录
    q.prepare(QStringLiteral(
        "SELECT rs.id, rs.book_id, b.title, rs.queue_no, rs.status, rs.requested_at, rs.pickup_deadline "
        "FROM reservations rs JOIN books b ON b.id = rs.book_id "
        "WHERE rs.reader_id = ? ORDER BY rs.requested_at DESC"));
    q.addBindValue(readerId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约失败: %1").arg(q.lastError().text()));
    QVariantList reservations;
    while (q.next()) {
        const QString status = q.value(4).toString();
        const QDateTime deadline = readDateTimeValue(q.value(6));
        QVariantMap row;
        row.insert(QStringLiteral("reservation_id"), q.value(0).toLongLong());
        row.insert(QStringLiteral("book_id"), q.value(1).toLongLong());
        row.insert(QStringLiteral("title"), q.value(2).toString());
        row.insert(QStringLiteral("queue_no"), q.value(3).toInt());
        row.insert(QStringLiteral("status"), status);
        row.insert(QStringLiteral("status_text"), resvStatusDisplayName(status));
        row.insert(QStringLiteral("requested_at"), readDateTimeValue(q.value(5)).toString(Qt::ISODate));
        row.insert(QStringLiteral("pickup_deadline"), deadline.isValid() ? deadline.toString(Qt::ISODate) : QString());
        reservations.append(row);
    }
    r.data.insert(QStringLiteral("reservations"), reservations);

    // 5) 逾期信息（实时判定，无费用）
    q.prepare(QStringLiteral(
        "SELECT l.id, b.title, l.due_at FROM loans l JOIN books b ON b.id = l.book_id "
        "WHERE l.reader_id = ? AND l.returned_at IS NULL AND l.due_at < ? ORDER BY l.due_at ASC"));
    q.addBindValue(readerId);
    q.addBindValue(today);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询逾期失败: %1").arg(q.lastError().text()));
    QVariantList overdue;
    while (q.next()) {
        const QDate dueAt = readDateValue(q.value(2));
        QVariantMap row;
        row.insert(QStringLiteral("loan_id"), q.value(0).toLongLong());
        row.insert(QStringLiteral("title"), q.value(1).toString());
        row.insert(QStringLiteral("due_at"), dueAt.toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("overdue_days"), dueAt.isValid() ? dueAt.daysTo(today) : 0);
        overdue.append(row);
    }
    r.data.insert(QStringLiteral("overdue"), overdue);

    return r;
}

} // namespace lib
