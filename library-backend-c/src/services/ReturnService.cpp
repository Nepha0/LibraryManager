#include "ReturnService.h"

#include "services/CopyService.h"   // A 端 CopyService（INCLUDEPATH 含 library-backend-a/src）

#include <QSqlError>

namespace lib {

ReturnService::ReturnService(QSqlDatabase db, CopyService* copies)
    : m_db(db), m_copies(copies) {}

// ===================== 归还 =====================
Result ReturnService::returnBook(const OperatorContext& op, const QVariantMap& locator)
{
    // 1) 操作者校验
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可办理归还"));

    // 2) 定位方式：借阅记录ID 或 副本条码（二选一）
    const qint64 locatorLoanId = locator.value(QStringLiteral("loan_id")).toLongLong();
    const QString barcode = locator.value(QStringLiteral("copy_barcode")).toString().trimmed();
    if (locatorLoanId <= 0 && barcode.isEmpty())
        return Result::fail(ErrorCode::MissingField, QStringLiteral("请提供借阅记录ID或副本条码"));

    if (!m_copies)
        return Result::fail(ErrorCode::DbError, QStringLiteral("馆藏服务不可用"));

    // 3) 事务开始
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启归还事务: %1").arg(m_db.lastError().text()));

    auto rollbackFail = [this](auto c, const QString& msg) {
        m_db.rollback();
        return Result::fail(toErrorCode(c), msg);
    };

    // 4) 定位唯一在借记录
    QSqlQuery q(m_db);
    if (locatorLoanId > 0) {
        q.prepare(QStringLiteral("SELECT l.id, l.copy_id, l.book_id, l.due_at FROM loans l "
                                 "WHERE l.id = ? AND l.returned_at IS NULL"));
        q.addBindValue(locatorLoanId);
    } else {
        // 条码定位：同一副本至多一条有效借阅（数据库唯一约束保证）
        q.prepare(QStringLiteral("SELECT l.id, l.copy_id, l.book_id, l.due_at FROM loans l "
                                 "JOIN book_copies bc ON bc.id = l.copy_id "
                                 "WHERE bc.copy_code = ? AND l.returned_at IS NULL LIMIT 1"));
        q.addBindValue(barcode);
    }
    if (!q.exec())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("查询借阅记录失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return rollbackFail(ErrorCodeC::LoanNotFound, QStringLiteral("借阅记录不存在或已归还"));

    const qint64 loanId  = q.value(0).toLongLong();
    const qint64 copyId  = q.value(1).toLongLong();
    const qint64 bookId  = q.value(2).toLongLong();
    const QDate  dueAt   = readDateValue(q.value(3));

    // 5) 更新归还日期；逾期天数 = 归还日 - 应还日（仅展示，无费用）
    const QDate today = QDate::currentDate();
    const int overdueDays = (dueAt.isValid() && dueAt < today) ? dueAt.daysTo(today) : 0;

    q.prepare(QStringLiteral("UPDATE loans SET returned_at = ? WHERE id = ? AND returned_at IS NULL"));
    q.addBindValue(today);
    q.addBindValue(loanId);
    if (!q.exec() || q.numRowsAffected() != 1)
        return rollbackFail(ErrorCode::DbError, QStringLiteral("更新借阅记录失败，请刷新后重试"));

    // 6) 预约队列：该书 waiting 队首（按申请时间排队；TODO(P1)：先惰性释放已超时的 ready_to_pick）
    q.prepare(QStringLiteral("SELECT id FROM reservations WHERE book_id = ? AND status = 'waiting' "
                             "ORDER BY requested_at ASC, id ASC LIMIT 1"));
    q.addBindValue(bookId);
    if (!q.exec())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("查询预约队列失败: %1").arg(q.lastError().text()));

    bool reservationTriggered = false;
    QString newCopyStatus;
    if (q.next()) {
        // 6a) 归还且有预约 → 副本转"预约保留待取"，队首转"待取"
        const qint64 resvId = q.value(0).toLongLong();
        Result cs = m_copies->changeCopyStatus(op, copyId, QStringLiteral("reserved"));
        if (!cs.ok())
            return rollbackFail(cs.code, cs.message);

        QString perr;
        const std::optional<int> holdDays =
            readIntParam(m_db, QStringLiteral("reservation_hold_days"), &perr);
        if (!holdDays || *holdDays < 1)
            return rollbackFail(ErrorCode::DbError,
                                perr.isEmpty() ? QStringLiteral("系统参数 reservation_hold_days 非法") : perr);

        const QDateTime deadline = QDateTime::currentDateTime().addDays(*holdDays);
        q.prepare(QStringLiteral("UPDATE reservations SET status = 'ready_to_pick', pickup_deadline = ? "
                                 "WHERE id = ? AND status = 'waiting'"));
        q.addBindValue(deadline);
        q.addBindValue(resvId);
        if (!q.exec() || q.numRowsAffected() != 1)
            return rollbackFail(ErrorCode::DbError, QStringLiteral("更新预约状态失败，请刷新后重试"));

        reservationTriggered = true;
        newCopyStatus = QStringLiteral("reserved");
    } else {
        // 6b) 无排队预约 → 副本回"在馆可借"
        Result cs = m_copies->changeCopyStatus(op, copyId, QStringLiteral("available"));
        if (!cs.ok())
            return rollbackFail(cs.code, cs.message);
        newCopyStatus = QStringLiteral("available");
    }

    // 7) 提交
    if (!m_db.commit())
        return rollbackFail(ErrorCode::DbError, QStringLiteral("归还事务提交失败: %1").arg(m_db.lastError().text()));

    Result r = Result::success(reservationTriggered
                                   ? QStringLiteral("归还成功，该副本已转为预约保留（待取）状态")
                                   : QStringLiteral("归还成功"));
    r.data.insert(QStringLiteral("loan_id"), loanId);
    r.data.insert(QStringLiteral("returned_at"), today.toString(QStringLiteral("yyyy-MM-dd")));
    r.data.insert(QStringLiteral("overdue_days"), overdueDays);
    r.data.insert(QStringLiteral("reservation_triggered"), reservationTriggered);
    r.data.insert(QStringLiteral("copy_status"), newCopyStatus);
    return r;
}

} // namespace lib
