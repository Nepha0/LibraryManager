#include "OverdueService.h"

#include <QSqlError>

namespace lib {

OverdueService::OverdueService(QSqlDatabase db)
    : m_db(db) {}

// ===================== 逾期识别与展示（实时判定，无费用） =====================
Result OverdueService::listOverdue(const OperatorContext& op, const QVariantMap& filter,
                                   int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可查看逾期管理"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    // mode：current=仅逾期在借（默认）；all=追加已归还的历史逾期
    QString mode = filter.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (mode.isEmpty()) mode = QStringLiteral("current");
    if (mode != QStringLiteral("current") && mode != QStringLiteral("all"))
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("未知的逾期查询模式：%1（可选 current/all）").arg(mode));

    QStringList where;
    QVariantList binds;
    const QDate today = QDate::currentDate();
    if (mode == QStringLiteral("current")) {
        where << QStringLiteral("l.returned_at IS NULL AND l.due_at < ?");
    } else {
        where << QStringLiteral("((l.returned_at IS NULL AND l.due_at < ?) "
                                 "OR (l.returned_at IS NOT NULL AND l.returned_at > l.due_at))");
    }
    binds << today;

    const QVariant readerId = filter.value(QStringLiteral("reader_id"));
    if (readerId.isValid() && readerId.toLongLong() > 0) {
        where << QStringLiteral("l.reader_id = ?");
        binds << readerId.toLongLong();
    }
    const QVariant bookId = filter.value(QStringLiteral("book_id"));
    if (bookId.isValid() && bookId.toLongLong() > 0) {
        where << QStringLiteral("l.book_id = ?");
        binds << bookId.toLongLong();
    }

    QString sql = QStringLiteral(
        "SELECT l.id, l.reader_id, r.reader_no, r.name, l.book_id, b.title, bc.copy_code AS barcode, "
        "       l.due_at, l.returned_at "
        "FROM loans l "
        "JOIN readers r ON r.id = l.reader_id "
        "JOIN books b ON b.id = l.book_id "
        "JOIN book_copies bc ON bc.id = l.copy_id "
        "WHERE ") + where.join(QStringLiteral(" AND "));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM (") + sql + QStringLiteral(") AS t"));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("统计逾期记录失败: %1").arg(q.lastError().text()));
    const int total = q.value(0).toInt();

    q.prepare(sql + QStringLiteral(" ORDER BY l.due_at ASC, l.id ASC LIMIT ? OFFSET ?"));
    for (const QVariant& b : binds) q.addBindValue(b);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询逾期记录失败: %1").arg(q.lastError().text()));

    Result r = Result::success();
    while (q.next()) {
        const QDate dueAt = readDateValue(q.value(7));
        const QDate returnedAt = readDateValue(q.value(8));
        const bool active = returnedAt.isNull();
        // 逾期天数：在借=今天-应还日；历史=归还日-应还日（无费用字段）
        const QDate end = active ? today : returnedAt;
        const int overdueDays = (dueAt.isValid() && end.isValid() && end > dueAt) ? dueAt.daysTo(end) : 0;

        QVariantMap row;
        row.insert(QStringLiteral("loan_id"), q.value(0).toLongLong());
        row.insert(QStringLiteral("reader_id"), q.value(1).toLongLong());
        row.insert(QStringLiteral("reader_no"), q.value(2).toString());
        row.insert(QStringLiteral("reader_name"), q.value(3).toString());
        row.insert(QStringLiteral("book_id"), q.value(4).toLongLong());
        row.insert(QStringLiteral("title"), q.value(5).toString());
        row.insert(QStringLiteral("barcode"), q.value(6).toString());
        row.insert(QStringLiteral("due_at"), dueAt.toString(QStringLiteral("yyyy-MM-dd")));
        row.insert(QStringLiteral("overdue_days"), overdueDays);
        row.insert(QStringLiteral("status_text"), active ? QStringLiteral("在借-逾期") : QStringLiteral("已归还"));
        r.list.append(row);
    }
    r.total = total;
    return r;
}

} // namespace lib
