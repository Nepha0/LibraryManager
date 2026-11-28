#include "ReservationService.h"

#include "services/CopyService.h"   // A 端 CopyService（INCLUDEPATH 含 library-backend-a/src）

#include <QSqlError>

namespace lib {

ReservationService::ReservationService(QSqlDatabase db, CopyService* copies, EligibilityFn eligibility)
    : m_db(db), m_copies(copies), m_eligibility(std::move(eligibility)) {}

// ===================== 私有：惰性释放该书已超时的待取预约（须在事务内调用） =====================
Result ReservationService::releaseExpiredReadyToPick(const OperatorContext& op, qint64 bookId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM reservations WHERE book_id = ? AND status = 'ready_to_pick' "
        "AND pickup_deadline IS NOT NULL AND pickup_deadline < ?"));
    q.addBindValue(bookId);
    q.addBindValue(QDateTime::currentDateTime());
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询超时预约失败: %1").arg(q.lastError().text()));

    while (q.next()) {
        const qint64 resvId = q.value(0).toLongLong();
        // 该书处于 reserved 的副本（预约保留状态由预约触发产生）
        QSqlQuery c(m_db);
        c.prepare(QStringLiteral("SELECT id FROM book_copies WHERE book_id = ? AND status = 'reserved' "
                                 "ORDER BY id LIMIT 1"));
        c.addBindValue(bookId);
        if (!c.exec() || !c.next())
            return Result::fail(ErrorCode::DbError, QStringLiteral("定位待释放副本失败: %1").arg(c.lastError().text()));
        const qint64 copyId = c.value(0).toLongLong();

        // 副本释放必须经 A 端状态机（reserved → available 合法）
        Result cs = m_copies->changeCopyStatus(op, copyId, QStringLiteral("available"));
        if (!cs.ok()) return cs;

        QSqlQuery u(m_db);
        u.prepare(QStringLiteral("UPDATE reservations SET status = 'expired' "
                                 "WHERE id = ? AND status = 'ready_to_pick'"));
        u.addBindValue(resvId);
        if (!u.exec() || u.numRowsAffected() != 1)
            return failC(ErrorCodeC::IllegalReservationTransition, QStringLiteral("预约状态已变化，超时释放已取消"));
    }
    return Result::success();
}

// ===================== 提交预约 =====================
Result ReservationService::reserve(const OperatorContext& op, qint64 readerId, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (readerId <= 0 || bookId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少读者或图书"));

    // 1) Reader 仅可为自己提交预约（业务层强制）
    if (op.role == Role::Reader) {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT user_id FROM readers WHERE id = ?"));
        q.addBindValue(readerId);
        if (!q.exec() || !q.next())
            return Result::fail(ErrorCode::ForeignKeyNotFound, QStringLiteral("读者不存在"));
        const QVariant uid = q.value(0);
        if (!uid.isValid() || uid.toLongLong() != op.userId)
            return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：读者仅可为自己提交预约"));
    }

    // 2) 读者资格（B 板块统一出口）
    if (m_eligibility) {
        const EligibilityResult e = m_eligibility(readerId);
        if (!e.eligible)
            return failC(ErrorCodeC::ReaderNotEligible,
                         e.reasons.isEmpty() ? QStringLiteral("读者资格不通过")
                                             : e.reasons.join(QStringLiteral("；")));
    }

    // 3) 图书存在且未下架
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT status FROM books WHERE id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::BookNotFound, QStringLiteral("图书不存在（book_id=%1）").arg(bookId));
    if (q.value(0).toString().toLower() == QStringLiteral("shelved_off"))
        return Result::fail(ErrorCode::BookShelvedOff, QStringLiteral("该图书已下架，不可预约"));

    // 4) 仅当无可借副本时可提交（A 端统计；键名 = 副本状态字面量）
    if (!m_copies)
        return Result::fail(ErrorCode::DbError, QStringLiteral("馆藏服务不可用"));
    Result st = m_copies->copyStatistics(op, bookId);
    if (!st.ok()) return st;
    const int available = st.data.value(QStringLiteral("available")).toInt();
    if (available > 0)
        return failC(ErrorCodeC::ReservationNotAllowed,
                     QStringLiteral("该书当前有 %1 本可借副本，不可提交预约").arg(available));

    // 5) 同一读者同书不可重复排队（uk_active_reservation 唯一约束兜底）
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE book_id = ? AND reader_id = ? "
                             "AND status IN ('waiting','ready_to_pick')"));
    q.addBindValue(bookId);
    q.addBindValue(readerId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("预约重复校验失败: %1").arg(q.lastError().text()));
    if (q.value(0).toInt() > 0)
        return failC(ErrorCodeC::DuplicateReservation, QStringLiteral("您对该书已有排队中/待取的预约，不可重复预约"));

    // 6) 事务：排队号 = 当前最大号 + 1
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启预约事务: %1").arg(m_db.lastError().text()));
    auto rollbackFail = [this](auto c, const QString& msg) {
        m_db.rollback();
        return Result::fail(toErrorCode(c), msg);
    };

    q.prepare(QStringLiteral("SELECT COALESCE(MAX(queue_no), 0) + 1 FROM reservations WHERE book_id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("分配排队号失败: %1").arg(q.lastError().text()));
    const int queueNo = q.value(0).toInt();

    const QDateTime now = QDateTime::currentDateTime();
    q.prepare(QStringLiteral("INSERT INTO reservations(book_id, reader_id, queue_no, status, requested_at) "
                             "VALUES(?, ?, ?, 'waiting', ?)"));
    q.addBindValue(bookId);
    q.addBindValue(readerId);
    q.addBindValue(queueNo);
    q.addBindValue(now);
    if (!q.exec())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("写入预约失败: %1").arg(q.lastError().text()));
    const qint64 resvId = q.lastInsertId().toLongLong();
    if (resvId <= 0)
        return rollbackFail(ErrorCode::DbError, QStringLiteral("无法读取新增预约编号"));

    if (!m_db.commit())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("预约事务提交失败: %1").arg(m_db.lastError().text()));

    Result r = Result::success(QStringLiteral("预约成功，已进入排队"));
    r.data.insert(QStringLiteral("reservation_id"), resvId);
    r.data.insert(QStringLiteral("queue_no"), queueNo);
    r.data.insert(QStringLiteral("requested_at"), now.toString(Qt::ISODate));
    return r;
}

// ===================== 取消预约 =====================
Result ReservationService::cancelReservation(const OperatorContext& op, qint64 reservationId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (reservationId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少预约ID"));

    // 加载预约与读者
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT rs.book_id, rs.reader_id, rs.status, r.user_id "
                             "FROM reservations rs JOIN readers r ON r.id = rs.reader_id WHERE rs.id = ?"));
    q.addBindValue(reservationId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return failC(ErrorCodeC::ReservationNotFound, QStringLiteral("预约记录不存在"));
    const qint64 bookId = q.value(0).toLongLong();
    const QString status = q.value(2).toString().toLower();
    const QVariant readerUserId = q.value(3);

    // Reader 仅可取消本人预约
    if (op.role == Role::Reader
        && (!readerUserId.isValid() || readerUserId.toLongLong() != op.userId))
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：读者仅可取消本人预约"));

    if (status == QStringLiteral("finished") || status == QStringLiteral("cancelled")
        || status == QStringLiteral("expired"))
        return failC(ErrorCodeC::IllegalReservationTransition,
                     QStringLiteral("预约已是终态（%1），不可取消").arg(resvStatusDisplayName(status)));

    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启取消事务: %1").arg(m_db.lastError().text()));
    auto rollbackFail = [this](auto c, const QString& msg) {
        m_db.rollback();
        return Result::fail(toErrorCode(c), msg);
    };

    if (status == QStringLiteral("ready_to_pick")) {
        // 待取取消：先释放副本（reserved → available，经 A 端状态机）
        QSqlQuery c(m_db);
        c.prepare(QStringLiteral("SELECT id FROM book_copies WHERE book_id = ? AND status = 'reserved' "
                                 "ORDER BY id LIMIT 1"));
        c.addBindValue(bookId);
        if (!c.exec() || !c.next())
            return rollbackFail(ErrorCode::DbError, QStringLiteral("定位待释放副本失败: %1").arg(c.lastError().text()));
        Result cs = m_copies->changeCopyStatus(op, c.value(0).toLongLong(), QStringLiteral("available"));
        if (!cs.ok()) return rollbackFail(cs.code, cs.message);
    }

    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("UPDATE reservations SET status = 'cancelled' WHERE id = ? AND status = ?"));
    u.addBindValue(reservationId);
    u.addBindValue(status);
    if (!u.exec() || u.numRowsAffected() != 1)
        return rollbackFail(ErrorCodeC::IllegalReservationTransition, QStringLiteral("预约状态已变化，取消已回滚"));

    if (!m_db.commit())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("取消事务提交失败: %1").arg(m_db.lastError().text()));

    return Result::success(QStringLiteral("预约已取消"));
}

// ===================== 处理：队首转待取 =====================
Result ReservationService::promoteNext(const OperatorContext& op, qint64 bookId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可处理预约"));
    if (bookId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少图书"));

    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启预约处理事务: %1").arg(m_db.lastError().text()));
    auto rollbackFail = [this](auto c, const QString& msg) {
        m_db.rollback();
        return Result::fail(toErrorCode(c), msg);
    };

    // 惰性释放之后的失败路径：释放结果已成立，须提交持久化（随事务回滚会丢失超时失效）
    auto commitFail = [this](auto c, const QString& msg) {
        m_db.commit();
        return Result::fail(toErrorCode(c), msg);
    };

    // 0) 先惰性释放该书已超时的待取预约（超时失效 → 副本回馆）
    Result rel = releaseExpiredReadyToPick(op, bookId);
    if (!rel.ok()) return rollbackFail(rel.code, rel.message);

    // 1) 队首 waiting
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, reader_id FROM reservations WHERE book_id = ? AND status = 'waiting' "
                             "ORDER BY requested_at ASC, id ASC LIMIT 1"));
    q.addBindValue(bookId);
    if (!q.exec())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("查询排队队列失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return commitFail(ErrorCodeC::NoQueueToPromote, QStringLiteral("该书无排队中的预约可处理"));
    const qint64 resvId = q.value(0).toLongLong();
    const qint64 readerId = q.value(1).toLongLong();

    // 2) 选一本在馆可借副本，经 A 端状态机转 reserved（available → reserved 合法）
    QSqlQuery c(m_db);
    c.prepare(QStringLiteral("SELECT id FROM book_copies WHERE book_id = ? AND status = 'available' "
                             "ORDER BY id LIMIT 1"));
    c.addBindValue(bookId);
    if (!c.exec() || !c.next())
        return commitFail(ErrorCodeC::CopyNotAvailable, QStringLiteral("该书当前无在馆可借副本，无法转待取"));
    const qint64 copyId = c.value(0).toLongLong();
    Result cs = m_copies->changeCopyStatus(op, copyId, QStringLiteral("reserved"));
    if (!cs.ok()) return rollbackFail(cs.code, cs.message);

    // 3) 待取截止时间 = 现在 + reservation_hold_days（参数）
    QString perr;
    const std::optional<int> holdDays = readIntParam(m_db, QStringLiteral("reservation_hold_days"), &perr);
    if (!holdDays || *holdDays < 1)
        return rollbackFail(ErrorCode::DbError,
                            perr.isEmpty() ? QStringLiteral("系统参数 reservation_hold_days 非法") : perr);
    const QDateTime deadline = QDateTime::currentDateTime().addDays(*holdDays);

    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("UPDATE reservations SET status = 'ready_to_pick', pickup_deadline = ? "
                             "WHERE id = ? AND status = 'waiting'"));
    u.addBindValue(deadline);
    u.addBindValue(resvId);
    if (!u.exec() || u.numRowsAffected() != 1)
        return rollbackFail(ErrorCodeC::IllegalReservationTransition, QStringLiteral("预约状态已变化，处理已回滚"));

    if (!m_db.commit())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("预约处理事务提交失败: %1").arg(m_db.lastError().text()));

    Result r = Result::success(QStringLiteral("队首预约已转为待取"));
    r.data.insert(QStringLiteral("reservation_id"), resvId);
    r.data.insert(QStringLiteral("reader_id"), readerId);
    r.data.insert(QStringLiteral("copy_id"), copyId);
    r.data.insert(QStringLiteral("pickup_deadline"), deadline.toString(Qt::ISODate));
    r.data.insert(QStringLiteral("status"), QStringLiteral("ready_to_pick"));
    return r;
}

// ===================== 完成（待取 → 已完成） =====================
Result ReservationService::finishReservation(const OperatorContext& op, qint64 reservationId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可完成预约"));
    if (reservationId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少预约ID"));

    // 正常路径：预约者取书借阅成功时由 BorrowService 自动完成；
    // 本方法供手工确认（不改变副本状态，副本由借阅/副本管理流程驱动）。
    QSqlQuery u(m_db);
    u.prepare(QStringLiteral("UPDATE reservations SET status = 'finished' WHERE id = ? AND status = 'ready_to_pick'"));
    u.addBindValue(reservationId);
    if (!u.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("更新预约失败: %1").arg(u.lastError().text()));
    if (u.numRowsAffected() != 1)
        return failC(ErrorCodeC::IllegalReservationTransition,
                     QStringLiteral("预约不存在或不处于待取状态，不可完成"));

    return Result::success(QStringLiteral("预约已完成"));
}

// ===================== 队列 / 列表查询 =====================
namespace {

QString resvRowSelect()
{
    return QStringLiteral(
        "SELECT rs.id, rs.reader_id, r.reader_no, r.name, rs.queue_no, rs.status, "
        "       rs.requested_at, rs.pickup_deadline, rs.book_id, b.title "
        "FROM reservations rs "
        "JOIN readers r ON r.id = rs.reader_id "
        "JOIN books b ON b.id = rs.book_id ");
}

QVariantMap resvRowFromQuery(QSqlQuery& q)
{
    QVariantMap row;
    const QString status = q.value(5).toString();
    const QDateTime deadline = readDateTimeValue(q.value(7));
    row.insert(QStringLiteral("reservation_id"), q.value(0).toLongLong());
    row.insert(QStringLiteral("reader_id"), q.value(1).toLongLong());
    row.insert(QStringLiteral("reader_no"), q.value(2).toString());
    row.insert(QStringLiteral("reader_name"), q.value(3).toString());
    row.insert(QStringLiteral("queue_no"), q.value(4).toInt());
    row.insert(QStringLiteral("status"), status);
    row.insert(QStringLiteral("status_text"), resvStatusDisplayName(status));
    row.insert(QStringLiteral("requested_at"), readDateTimeValue(q.value(6)).toString(Qt::ISODate));
    row.insert(QStringLiteral("pickup_deadline"), deadline.isValid() ? deadline.toString(Qt::ISODate) : QString());
    row.insert(QStringLiteral("book_id"), q.value(8).toLongLong());
    row.insert(QStringLiteral("title"), q.value(9).toString());
    return row;
}

} // namespace

Result ReservationService::listReservationQueue(const OperatorContext& op, qint64 bookId,
                                                int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (bookId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少图书"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    // 待取优先、其余按排队号/申请时间（CASE 排序 MySQL/SQLite 通用）
    const QString sql = resvRowSelect()
        + QStringLiteral("WHERE rs.book_id = ? "
                         "ORDER BY CASE rs.status WHEN 'ready_to_pick' THEN 0 WHEN 'waiting' THEN 1 ELSE 2 END, "
                         "rs.requested_at ASC, rs.id ASC ");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM reservations WHERE book_id = ?"));
    q.addBindValue(bookId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("统计预约失败: %1").arg(q.lastError().text()));
    const int total = q.value(0).toInt();

    q.prepare(sql + QStringLiteral("LIMIT ? OFFSET ?"));
    q.addBindValue(bookId);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约队列失败: %1").arg(q.lastError().text()));

    Result r = Result::success();
    while (q.next())
        r.list.append(resvRowFromQuery(q));
    r.total = total;
    return r;
}

Result ReservationService::listReservations(const OperatorContext& op, const QVariantMap& filter,
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
        where << QStringLiteral("rs.reader_id = ?");
        binds << readerId.toLongLong();
    }
    const QVariant bookId = filter.value(QStringLiteral("book_id"));
    if (bookId.isValid() && bookId.toLongLong() > 0) {
        where << QStringLiteral("rs.book_id = ?");
        binds << bookId.toLongLong();
    }
    const QString status = filter.value(QStringLiteral("status")).toString().trimmed().toLower();
    if (!status.isEmpty()) {
        bool ok = false;
        const ResvStatus rs = resvStatusFromString(status, &ok);
        if (!ok)
            return Result::fail(ErrorCode::InvalidCondition,
                                QStringLiteral("未知的预约状态：%1").arg(status));
        where << QStringLiteral("rs.status = ?");
        binds << resvStatusToString(rs);
    }
    // Reader 只能查看本人预约
    if (op.role == Role::Reader) {
        where << QStringLiteral("r.user_id = ?");
        binds << op.userId;
    }

    QString sql = resvRowSelect();
    if (!where.isEmpty())
        sql += QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY rs.requested_at DESC, rs.id DESC ");

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM reservations rs JOIN readers r ON r.id = rs.reader_id ")
              + (where.isEmpty() ? QString() : QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "))));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("统计预约失败: %1").arg(q.lastError().text()));
    const int total = q.value(0).toInt();

    q.prepare(sql + QStringLiteral("LIMIT ? OFFSET ?"));
    for (const QVariant& b : binds) q.addBindValue(b);
    q.addBindValue(pageSize);
    q.addBindValue((page - 1) * pageSize);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约失败: %1").arg(q.lastError().text()));

    Result r = Result::success();
    while (q.next())
        r.list.append(resvRowFromQuery(q));
    r.total = total;
    return r;
}

} // namespace lib
