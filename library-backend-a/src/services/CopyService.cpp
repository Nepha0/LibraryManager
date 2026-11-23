#include "CopyService.h"
#include <QSqlQuery>
#include <QSqlError>
#include "../database/DatabaseManager.h"

namespace lib {

CopyService::CopyService(QSqlDatabase db) : m_db(db) {}

// ===================== 状态机：唯一合法转换源 =====================
bool CopyService::isTransitionLegal(CopyStatus from, CopyStatus to)
{
    switch (from) {
    case CopyStatus::Available: // 在馆可借 → 借出/预约保留/维修中/遗失报损
        return to == CopyStatus::Borrowed || to == CopyStatus::Reserved
            || to == CopyStatus::Maintenance || to == CopyStatus::Lost;
    case CopyStatus::Borrowed:  // 借出 → 在馆可借（归还）/ 预约保留待取（归还且有预约）
        return to == CopyStatus::Available || to == CopyStatus::Reserved;
    case CopyStatus::Reserved:  // 预约保留待取 → 借出（取书）/ 在馆可借（超时释放或取消）
        return to == CopyStatus::Borrowed || to == CopyStatus::Available;
    case CopyStatus::Maintenance: // 维修中 → 仅管理员恢复(AVAILABLE)或终结(LOST)
        return to == CopyStatus::Available || to == CopyStatus::Lost;
    case CopyStatus::Lost:        // 遗失报损 → 仅管理员恢复(AVAILABLE)
        return to == CopyStatus::Available;
    }
    return false;
}

// ===================== 私有：按主键 / 条码取副本 =====================
Result CopyService::fetchCopyRow(qint64 copyId, QVariantMap* out)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT cp.id, cp.copy_code AS barcode, cp.book_id, b.title AS book_title, b.status AS book_status,"
        " cp.location, cp.status, cp.acquired_date"
        " FROM book_copies cp LEFT JOIN books b ON b.id = cp.book_id WHERE cp.id = ?"));
    q.addBindValue(copyId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::CopyNotFound,
                            QStringLiteral("副本不存在（id=%1）").arg(copyId));
    QVariantMap row;
    row.insert(QStringLiteral("copy_id"),      q.value(QStringLiteral("id")));
    row.insert(QStringLiteral("barcode"),      q.value(QStringLiteral("barcode")));
    row.insert(QStringLiteral("book_id"),      q.value(QStringLiteral("book_id")));
    row.insert(QStringLiteral("book_title"),   q.value(QStringLiteral("book_title")));
    row.insert(QStringLiteral("book_status"),  q.value(QStringLiteral("book_status")));
    row.insert(QStringLiteral("location"),     q.value(QStringLiteral("location")));
    row.insert(QStringLiteral("status"),       q.value(QStringLiteral("status")));
    row.insert(QStringLiteral("status_text"),
               copyStatusDisplayName(q.value(QStringLiteral("status")).toString()));
    row.insert(QStringLiteral("acquired_date"), q.value(QStringLiteral("acquired_date")));
    if (out) *out = row;
    Result r = Result::success();
    r.data = row;
    return r;
}

Result CopyService::fetchCopyByBarcode(const QString& barcode, QVariantMap* out)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM book_copies WHERE copy_code = ?"));
    q.addBindValue(barcode);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::CopyNotFound,
                            QStringLiteral("副本不存在（条码=%1）").arg(barcode));
    return fetchCopyRow(q.value(0).toLongLong(), out);
}

// ===================== 新增副本 =====================
Result CopyService::addCopy(const OperatorContext& op, qint64 bookId, const QString& barcode,
                            const QString& location, const QDate& acquiredDate)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可新增副本"));

    const QString bc = barcode.trimmed();
    if (bc.isEmpty())
        return Result::fail(ErrorCode::MissingField, QStringLiteral("必填字段缺失：副本条码"));

    // 图书外键校验
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT title, status FROM books WHERE id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::ForeignKeyNotFound,
                            QStringLiteral("图书不存在（book_id=%1）").arg(bookId));
    const QString bookTitle = q.value(0).toString();
    const bool shelved = q.value(1).toString() == QStringLiteral("shelved_off");
    if (shelved)
        return Result::fail(ErrorCode::BookShelvedOff,
                            QStringLiteral("该图书已下架，不能新增副本"));

    // 条码唯一校验
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM book_copies WHERE copy_code = ?"));
    q.addBindValue(bc);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("条码查重失败"));
    if (q.value(0).toInt() > 0)
        return Result::fail(ErrorCode::BarcodeDuplicate,
                            QStringLiteral("条码 %1 已存在").arg(bc));

    q.prepare(QStringLiteral(
        "INSERT INTO book_copies (copy_code, book_id, location, status, acquired_date)"
        " VALUES (?, ?, ?, 'available', ?)"));
    q.addBindValue(bc);
    q.addBindValue(bookId);
    // location 列 NOT NULL DEFAULT ''：空入参必须写空串而非 NULL（空 QVariant 会被 SQLite 存成 NULL）
    const QString loc = location.trimmed();
    q.addBindValue(loc.isEmpty() ? QVariant(QStringLiteral("")) : QVariant(loc));
    q.addBindValue(acquiredDate.isValid()
                       ? QVariant(acquiredDate.toString(QStringLiteral("yyyy-MM-dd")))
                       : QVariant(QString()));
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("新增副本失败: %1").arg(q.lastError().text()));

    Result r = fetchCopyRow(q.lastInsertId().toLongLong(), nullptr);
    r.message = QStringLiteral("副本 %1 已入藏《%2》").arg(bc, bookTitle);
    return r;
}

// ===================== 编辑副本基本信息 =====================
Result CopyService::updateCopy(const OperatorContext& op, qint64 copyId, const QVariantMap& fields)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可编辑副本"));

    QVariantMap existing;
    Result f = fetchCopyRow(copyId, &existing);
    if (!f.ok()) return f;

    // 状态字段不可经 updateCopy 修改，必须走状态机
    if (fields.contains(QStringLiteral("status")))
        return Result::fail(ErrorCode::IllegalStatusTransition,
                            QStringLiteral("副本状态请通过 changeCopyStatus 接口维护（当前状态：%1）")
                                .arg(copyStatusDisplayName(existing.value(QStringLiteral("status")).toString())));

    QStringList sets;
    QVariantList binds;
    if (fields.contains(QStringLiteral("location"))) {
        sets << QStringLiteral("location = ?");
        binds << fields.value(QStringLiteral("location")).toString().trimmed();
    }
    if (fields.contains(QStringLiteral("acquired_date"))) {
        const QString d = fields.value(QStringLiteral("acquired_date")).toString().trimmed();
        if (!d.isEmpty() && QDate::fromString(d, QStringLiteral("yyyy-MM-dd")).isNull())
            return Result::fail(ErrorCode::InvalidCondition,
                                QStringLiteral("入藏日期格式应为 yyyy-MM-dd"));
        sets << QStringLiteral("acquired_date = ?");
        binds << d;
    }
    if (sets.isEmpty())
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("未提供任何可更新的字段（仅支持 location / acquired_date）"));

    sets << QStringLiteral("updated_at = ") + DatabaseManager::instance().nowExpr();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE book_copies SET %1 WHERE id = %2")
                  .arg(sets.join(QStringLiteral(", "))).arg(copyId));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("更新副本失败: %1").arg(q.lastError().text()));
    return fetchCopyRow(copyId, nullptr);
}

// ===================== 状态变更 =====================
Result CopyService::changeCopyStatus(const OperatorContext& op, qint64 copyId, const QString& newStatus)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可变更副本状态"));

    bool parseOk = false;
    const CopyStatus to = copyStatusFromString(newStatus.trimmed().toUpper(), &parseOk);
    if (!parseOk)
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("未知的副本状态：%1").arg(newStatus));

    QVariantMap existing;
    Result f = fetchCopyRow(copyId, &existing);
    if (!f.ok()) return f;

    const CopyStatus from = copyStatusFromString(existing.value(QStringLiteral("status")).toString());
    const QString fromName = copyStatusToString(from);
    const QString toName   = copyStatusToString(to);
    const QString fromDisp = copyStatusDisplayName(fromName);
    const QString toDisp   = copyStatusDisplayName(toName);

    // 1) 状态机合法性
    if (from == to)
        return Result::fail(ErrorCode::IllegalStatusTransition,
                            QStringLiteral("非法状态转换：%1 → %1（状态未发生变化，当前状态：%1）").arg(fromDisp));
    if (!isTransitionLegal(from, to))
        return Result::fail(ErrorCode::IllegalStatusTransition,
                            QStringLiteral("非法状态转换：不允许从「%1」变更为「%2」（当前状态：%1）")
                                .arg(fromDisp, toDisp));

    // 2) 维修中 / 遗失报损 的恢复或终结：仅管理员
    if ((from == CopyStatus::Maintenance || from == CopyStatus::Lost) && !op.isAdmin())
        return Result::fail(ErrorCode::NoPermission,
                            QStringLiteral("无权限：「维修中/遗失报损」状态的副本仅管理员可手工恢复或终结"));

    // 3) 下架图书不可再借出（防御校验，借阅流程为 C 板块）
    if (to == CopyStatus::Borrowed
        && existing.value(QStringLiteral("book_status")).toString() == QStringLiteral("shelved_off"))
        return Result::fail(ErrorCode::BookShelvedOff,
                            QStringLiteral("该图书已下架，不可再借出"));

    // 4) 乐观锁更新：WHERE 带上当前状态，防止并发下状态漂移
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE book_copies SET status = ?, updated_at = ")
                  + DatabaseManager::instance().nowExpr()
                  + QStringLiteral(" WHERE id = ? AND status = ?"));
    q.addBindValue(toName);
    q.addBindValue(copyId);
    q.addBindValue(fromName);
    if (!q.exec() || q.numRowsAffected() != 1)
        return Result::fail(ErrorCode::IllegalStatusTransition,
                            QStringLiteral("副本状态已被其他操作变更，请刷新后重试（当前状态：%1）").arg(fromDisp));
    return fetchCopyRow(copyId, nullptr);
}

// ===================== 按图书查副本 =====================
Result CopyService::listCopiesByBook(const OperatorContext& op, qint64 bookId,
                                     int page, int pageSize)
{
    QVariantMap filter;
    filter.insert(QStringLiteral("book_id"), bookId);
    return listCopies(op, filter, page, pageSize);
}

// ===================== 通用副本查询 =====================
Result CopyService::listCopies(const OperatorContext& op, const QVariantMap& filter,
                               int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    QStringList where;
    QVariantList binds;
    if (filter.contains(QStringLiteral("book_id")) && !filter.value(QStringLiteral("book_id")).isNull()) {
        bool convOk = false;
        qint64 bid = filter.value(QStringLiteral("book_id")).toLongLong(&convOk);
        if (convOk && bid > 0) {
            where << QStringLiteral("cp.book_id = ?");
            binds << bid;
        }
    }
    const QString status = filter.value(QStringLiteral("status")).toString().trimmed();
    if (!status.isEmpty()) {
        bool ok = false;
        const CopyStatus cs = copyStatusFromString(status, &ok);
        if (!ok)
            return Result::fail(ErrorCode::InvalidCondition,
                                QStringLiteral("未知的副本状态：%1").arg(status));
        where << QStringLiteral("cp.status = ?");
        binds << copyStatusToString(cs);   // 库内存小写，绑定值须规范化
    }
    const QString loc = filter.value(QStringLiteral("location")).toString().trimmed();
    if (!loc.isEmpty()) {
        where << QStringLiteral("cp.location LIKE ?");
        binds << QStringLiteral("%%1%").arg(loc);
    }
    const QString bc = filter.value(QStringLiteral("barcode")).toString().trimmed();
    if (!bc.isEmpty()) {
        where << QStringLiteral("cp.copy_code LIKE ?");
        binds << QStringLiteral("%%1%").arg(bc);
    }
    const QString whereSql =
        where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM book_copies cp%1").arg(whereSql));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("统计副本总数失败: %1").arg(q.lastError().text()));
    Result r;
    r.total = q.value(0).toInt();

    q.prepare(QStringLiteral(
        "SELECT cp.id, cp.copy_code AS barcode, cp.book_id, b.title AS book_title, cp.location,"
        " cp.status, cp.acquired_date"
        " FROM book_copies cp LEFT JOIN books b ON b.id = cp.book_id%1"
        " ORDER BY cp.id LIMIT %2 OFFSET %3")
        .arg(whereSql).arg(pageSize).arg((page - 1) * pageSize));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("查询副本列表失败: %1").arg(q.lastError().text()));

    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("copy_id"),      q.value(QStringLiteral("id")));
        row.insert(QStringLiteral("barcode"),      q.value(QStringLiteral("barcode")));
        row.insert(QStringLiteral("book_id"),      q.value(QStringLiteral("book_id")));
        row.insert(QStringLiteral("book_title"),   q.value(QStringLiteral("book_title")));
        row.insert(QStringLiteral("location"),     q.value(QStringLiteral("location")));
        row.insert(QStringLiteral("status"),       q.value(QStringLiteral("status")));
        row.insert(QStringLiteral("status_text"),
                   copyStatusDisplayName(q.value(QStringLiteral("status")).toString()));
        row.insert(QStringLiteral("acquired_date"), q.value(QStringLiteral("acquired_date")));
        r.list << row;
    }

    // 若按图书查询，附带该书统计
    if (filter.contains(QStringLiteral("book_id")) && !filter.value(QStringLiteral("book_id")).isNull()) {
        QSqlQuery s(m_db);
        s.prepare(QStringLiteral(
            "SELECT status, COUNT(*) FROM book_copies WHERE book_id = ? GROUP BY status"));
        s.addBindValue(filter.value(QStringLiteral("book_id")));
        s.exec();
        QVariantMap stats;
        while (s.next())
            stats.insert(copyStatusToString(copyStatusFromString(s.value(0).toString())), s.value(1).toInt());
        r.data.insert(QStringLiteral("copy_status_stats"), stats);
    }
    return r;
}

// ===================== 按条码定位（供 C 板块借还流程） =====================
Result CopyService::getCopyByBarcode(const OperatorContext& op, const QString& barcode)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    QVariantMap row;
    Result r = fetchCopyByBarcode(barcode.trimmed(), &row);
    if (!r.ok()) return r;
    r.data = row;
    return r;
}

// ===================== 按图书统计各状态数量 =====================
Result CopyService::copyStatistics(const OperatorContext& op, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM books WHERE id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::BookNotFound,
                            QStringLiteral("图书不存在（book_id=%1）").arg(bookId));

    q.prepare(QStringLiteral(
        "SELECT status, COUNT(*) FROM book_copies WHERE book_id = ? GROUP BY status"));
    q.addBindValue(bookId);
    q.exec();
    QVariantMap stats;
    int total = 0;
    while (q.next()) {
        stats.insert(copyStatusToString(copyStatusFromString(q.value(0).toString())),
                     q.value(1).toInt());
        total += q.value(1).toInt();
    }
    Result r = Result::success();
    r.data = stats;
    r.data.insert(QStringLiteral("copy_total"), total);
    return r;
}

} // namespace lib
