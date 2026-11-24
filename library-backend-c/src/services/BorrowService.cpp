#include "BorrowService.h"

#include "services/CopyService.h"   // A 端 CopyService（INCLUDEPATH 含 library-backend-a/src）

#include <QSqlError>

namespace lib {

BorrowService::BorrowService(QSqlDatabase db, CopyService* copies, EligibilityFn eligibility)
    : m_db(db), m_copies(copies), m_eligibility(std::move(eligibility)) {}

// ===================== 借阅 =====================
Result BorrowService::borrow(const OperatorContext& op, qint64 readerId, const QString& copyBarcode)
{
    // 1) 操作者校验
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可办理借阅"));
    if (readerId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少读者"));
    const QString barcode = copyBarcode.trimmed();
    if (barcode.isEmpty())
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少副本条码"));

    // 2) 读者资格判断（B 板块统一出口，C 不重复实现读者侧规则）
    if (m_eligibility) {
        const EligibilityResult e = m_eligibility(readerId);
        if (!e.eligible) {
            return failC(ErrorCodeC::ReaderNotEligible,
                         e.reasons.isEmpty() ? QStringLiteral("读者资格不通过")
                                             : e.reasons.join(QStringLiteral("；")));
        }
    }

    // 3) 副本校验（A 板块；getCopyByBarcode 的 data 含 copy_id / status / book_status）
    if (!m_copies)
        return Result::fail(ErrorCode::CopyNotFound, QStringLiteral("馆藏服务不可用"));
    Result cr = m_copies->getCopyByBarcode(op, barcode);
    if (!cr.ok())
        return cr;   // 1011 CopyNotFound 原样透出
    const QVariantMap copy = cr.data;
    const QString copyStatus = copy.value(QStringLiteral("status")).toString().toLower();
    if (copy.value(QStringLiteral("book_status")).toString().toLower() == QStringLiteral("shelved_off"))
        return Result::fail(ErrorCode::BookShelvedOff, QStringLiteral("该图书已下架，不可再借出"));

    qint64 pickupResvId = 0;   // 预约取书分支：该副本对应的 ready_to_pick 预约
    if (copyStatus == QStringLiteral("reserved")) {
        // 3a) 预约者取书：副本当前即 reserved，合法转换 reserved→borrowed。
        //     仅该预约的读者本人可取，且预约须未超时。
        QSqlQuery rq(m_db);
        rq.prepare(QStringLiteral(
            "SELECT id, reader_id, pickup_deadline FROM reservations "
            "WHERE book_id = ? AND status = 'ready_to_pick' ORDER BY id LIMIT 1"));
        rq.addBindValue(copy.value(QStringLiteral("book_id")).toLongLong());
        if (!rq.exec())
            return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约状态失败: %1").arg(rq.lastError().text()));
        if (!rq.next())
            return failC(ErrorCodeC::CopyNotAvailable,
                         QStringLiteral("该副本为预约保留状态，但无有效待取预约（可能已超时失效）"));
        const qint64 resvReaderId = rq.value(1).toLongLong();
        const QDateTime deadline = readDateTimeValue(rq.value(2));
        if (deadline.isValid() && deadline < QDateTime::currentDateTime())
            return failC(ErrorCodeC::CopyNotAvailable, QStringLiteral("该预约已超过待取截止时间，已失效"));
        if (resvReaderId != readerId)
            return failC(ErrorCodeC::CopyNotAvailable, QStringLiteral("该副本为预约保留，仅预约读者本人可取书借阅"));
        pickupResvId = rq.value(0).toLongLong();
    } else if (copyStatus != QStringLiteral("available")) {
        // 3b) 普通借阅：副本必须是在馆可借
        return failC(ErrorCodeC::CopyNotAvailable,
                     QStringLiteral("该副本当前不可借（状态：%1）").arg(copyStatusDisplayName(copyStatus)));
    }
    const qint64 copyId = copy.value(QStringLiteral("copy_id")).toLongLong();
    const qint64 bookId = copy.value(QStringLiteral("book_id")).toLongLong();

    // 4) 事务开始
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启借阅事务: %1").arg(m_db.lastError().text()));

    auto rollbackFail = [this](auto c, const QString& msg) {
        m_db.rollback();
        return Result::fail(toErrorCode(c), msg);
    };

    // 5) 双保险：该副本不允许两条有效借阅（数据库唯一约束兜底）
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM loans WHERE copy_id = ? AND returned_at IS NULL"));
    q.addBindValue(copyId);
    if (!q.exec()) return rollbackFail(ErrorCode::DbError, QStringLiteral("借阅预检失败: %1").arg(q.lastError().text()));
    if (q.next() && q.value(0).toInt() > 0)
        return rollbackFail(ErrorCodeC::CopyNotAvailable, QStringLiteral("该副本已存在在借记录，不可重复借出"));

    // 6) 借阅期限（系统参数，不硬编码）；日期在 C++ 计算后绑定，避免方言差异
    QString perr;
    const std::optional<int> periodDays = readIntParam(m_db, QStringLiteral("borrow_period_days"), &perr);
    if (!periodDays || *periodDays < 1)
        return rollbackFail(ErrorCode::DbError,
                            perr.isEmpty() ? QStringLiteral("系统参数 borrow_period_days 非法") : perr);

    const QDate today = QDate::currentDate();
    const QDate dueDate = today.addDays(*periodDays);

    q.prepare(QStringLiteral("INSERT INTO loans(reader_id, copy_id, book_id, borrowed_at, due_at) "
                             "VALUES(?, ?, ?, ?, ?)"));
    q.addBindValue(readerId);
    q.addBindValue(copyId);
    q.addBindValue(bookId);
    q.addBindValue(today);
    q.addBindValue(dueDate);
    if (!q.exec())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("写入借阅记录失败: %1").arg(q.lastError().text()));
    const qint64 loanId = q.lastInsertId().toLongLong();
    if (loanId <= 0)
        return rollbackFail(ErrorCode::DbError, QStringLiteral("无法读取新增借阅记录编号"));

    // 7) 副本状态 → borrowed（A 端状态机 + 乐观锁；并发双借后到者 1007 → 回滚）
    Result cs = m_copies->changeCopyStatus(op, copyId, QStringLiteral("borrowed"));
    if (!cs.ok())
        return rollbackFail(cs.code, cs.message);

    // 8) 预约取书分支：待取预约转"已完成"
    if (pickupResvId > 0) {
        q.prepare(QStringLiteral("UPDATE reservations SET status = 'finished' "
                                 "WHERE id = ? AND status = 'ready_to_pick'"));
        q.addBindValue(pickupResvId);
        if (!q.exec() || q.numRowsAffected() != 1)
            return rollbackFail(ErrorCodeC::IllegalReservationTransition,
                                QStringLiteral("待取预约状态已变化，借阅已回滚"));
    }

    // 9) 提交
    if (!m_db.commit())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("借阅事务提交失败: %1").arg(m_db.lastError().text()));

    Result r = Result::success(pickupResvId > 0 ? QStringLiteral("预约取书成功，借阅已办理")
                                                : QStringLiteral("借阅成功"));
    r.data.insert(QStringLiteral("loan_id"), loanId);
    r.data.insert(QStringLiteral("reader_id"), readerId);
    r.data.insert(QStringLiteral("copy_id"), copyId);
    r.data.insert(QStringLiteral("book_id"), bookId);
    r.data.insert(QStringLiteral("borrowed_at"), today.toString(QStringLiteral("yyyy-MM-dd")));
    r.data.insert(QStringLiteral("due_at"), dueDate.toString(QStringLiteral("yyyy-MM-dd")));
    return r;
}

// ===================== 借阅记录行查询 =====================
namespace {

// 行结构：loan_id, reader_id, reader_no, reader_name, book_id, title, barcode,
//         borrowed_at, due_at, returned_at(空串=在借), overdue_days, renew_count, status_text
QString loanRowSelect()
{
    return QStringLiteral(
        "SELECT l.id, l.reader_id, r.reader_no, r.name, l.book_id, b.title, bc.copy_code AS barcode, "
        "       l.borrowed_at, l.due_at, l.returned_at, l.renew_count "
        "FROM loans l "
        "JOIN readers r ON r.id = l.reader_id "
        "JOIN books b ON b.id = l.book_id "
        "JOIN book_copies bc ON bc.id = l.copy_id ");
}

QVariantMap loanRowFromQuery(QSqlQuery& q)
{
    QVariantMap row;
    const QDate today = QDate::currentDate();
    const QDate dueAt = readDateValue(q.value(8));
    const QVariant returnedRaw = q.value(9);
    const QDate returnedAt = readDateValue(returnedRaw);
    const bool active = returnedAt.isNull();

    int overdueDays = 0;
    if (active && dueAt.isValid() && dueAt < today)
        overdueDays = dueAt.daysTo(today);
    else if (!active && dueAt.isValid() && returnedAt.isValid() && returnedAt > dueAt)
        overdueDays = dueAt.daysTo(returnedAt);

    row.insert(QStringLiteral("loan_id"), q.value(0).toLongLong());
    row.insert(QStringLiteral("reader_id"), q.value(1).toLongLong());
    row.insert(QStringLiteral("reader_no"), q.value(2).toString());
    row.insert(QStringLiteral("reader_name"), q.value(3).toString());
    row.insert(QStringLiteral("book_id"), q.value(4).toLongLong());
    row.insert(QStringLiteral("title"), q.value(5).toString());
    row.insert(QStringLiteral("barcode"), q.value(6).toString());
    row.insert(QStringLiteral("borrowed_at"), readDateValue(q.value(7)).toString(QStringLiteral("yyyy-MM-dd")));
    row.insert(QStringLiteral("due_at"), dueAt.toString(QStringLiteral("yyyy-MM-dd")));
    row.insert(QStringLiteral("returned_at"),
               active ? QString() : returnedAt.toString(QStringLiteral("yyyy-MM-dd")));
    row.insert(QStringLiteral("overdue_days"), overdueDays);
    row.insert(QStringLiteral("renew_count"), q.value(10).toInt());
    row.insert(QStringLiteral("status_text"),
               !active ? QStringLiteral("已还")
                       : (overdueDays > 0 ? QStringLiteral("逾期") : QStringLiteral("在借")));
    return row;
}

} // namespace

Result BorrowService::getLoan(const OperatorContext& op, qint64 loanId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (loanId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少借阅记录ID"));

    QSqlQuery q(m_db);
    q.prepare(loanRowSelect() + QStringLiteral("WHERE l.id = ?"));
    q.addBindValue(loanId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询借阅记录失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return failC(ErrorCodeC::LoanNotFound, QStringLiteral("借阅记录不存在"));

    Result r = Result::success();
    r.data = loanRowFromQuery(q);
    return r;
}

// ===================== 借阅记录查询（筛选 + 分页） =====================
Result BorrowService::listLoans(const OperatorContext& op, const QVariantMap& filter,
                                int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    QStringList where;
    QVariantList binds;
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
    const QString barcode = filter.value(QStringLiteral("barcode")).toString().trimmed();
    if (!barcode.isEmpty()) {
        where << QStringLiteral("bc.copy_code LIKE ?");
        binds << QStringLiteral("%%1%").arg(barcode);
    }
    const QString readerNo = filter.value(QStringLiteral("reader_no")).toString().trimmed();
    if (!readerNo.isEmpty()) {
        where << QStringLiteral("r.reader_no LIKE ?");
        binds << QStringLiteral("%%1%").arg(readerNo);
    }
    const QString from = filter.value(QStringLiteral("from")).toString().trimmed();
    if (!from.isEmpty()) {
        where << QStringLiteral("l.borrowed_at >= ?");
        binds << QDate::fromString(from, QStringLiteral("yyyy-MM-dd"));
    }
    const QString to = filter.value(QStringLiteral("to")).toString().trimmed();
    if (!to.isEmpty()) {
        where << QStringLiteral("l.borrowed_at <= ?");
        binds << QDate::fromString(to, QStringLiteral("yyyy-MM-dd"));
    }
    // 状态口径：active=在借（含逾期内判断）、returned=已还、overdue=在借且应还日期<今天（实时判定）
    const QString status = filter.value(QStringLiteral("status")).toString().trimmed().toLower();
    if (status == QStringLiteral("active")) {
        where << QStringLiteral("l.returned_at IS NULL");
    } else if (status == QStringLiteral("returned")) {
        where << QStringLiteral("l.returned_at IS NOT NULL");
    } else if (status == QStringLiteral("overdue")) {
        where << QStringLiteral("l.returned_at IS NULL AND l.due_at < ?");
        binds << QDate::currentDate();
    } else if (!status.isEmpty()) {
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("未知的借阅状态筛选：%1（可选 active/returned/overdue）").arg(status));
    }

    QString sql = loanRowSelect();
    if (!where.isEmpty())
        sql += QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "));

    // 总数
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM (") + sql + QStringLiteral(") AS t"));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("统计借阅记录失败: %1").arg(q.lastError().text()));
    const int total = q.value(0).toInt();

    // 当前页
    q.prepare(sql + QStringLiteral(" ORDER BY l.borrowed_at DESC, l.id DESC LIMIT ? OFFSET ?"));
    for (const QVariant& b : binds) q.addBindValue(b);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询借阅记录失败: %1").arg(q.lastError().text()));

    Result r = Result::success();
    while (q.next())
        r.list.append(loanRowFromQuery(q));
    r.total = total;
    return r;
}

} // namespace lib
