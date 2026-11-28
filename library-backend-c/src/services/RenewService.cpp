#include "RenewService.h"

#include "services/CopyService.h"        // 惰性清理用（A 端状态机）
#include "services/ReservationService.h" // 续借入口惰性释放超时待取

#include <QSqlError>

namespace lib {

RenewService::RenewService(QSqlDatabase db, EligibilityFn eligibility)
    : m_db(db), m_eligibility(std::move(eligibility)) {}

// ===================== 续借 =====================
Result RenewService::renew(const OperatorContext& op, qint64 loanId)
{
    // 1) 操作者校验
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (loanId <= 0)
        return Result::fail(ErrorCode::MissingField, QStringLiteral("缺少借阅记录ID"));

    // 2) 加载借阅记录（含读者 user_id，用于越权判定）
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT l.reader_id, l.book_id, l.due_at, l.returned_at, l.renew_count, r.user_id "
        "FROM loans l JOIN readers r ON r.id = l.reader_id WHERE l.id = ?"));
    q.addBindValue(loanId);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询借阅记录失败: %1").arg(q.lastError().text()));
    if (!q.next())
        return failC(ErrorCodeC::LoanNotFound, QStringLiteral("借阅记录不存在"));

    const qint64 readerId = q.value(0).toLongLong();
    const qint64 bookId = q.value(1).toLongLong();
    const QDate dueAt = readDateValue(q.value(2));
    const QDate returnedAt = readDateValue(q.value(3));
    const int renewCount = q.value(4).toInt();
    const QVariant readerUserId = q.value(5);
    const bool active = returnedAt.isNull();

    // 3) 权限：staff 可续借任意记录；Reader 仅可续借本人记录
    if (op.role == Role::Reader
        && (!readerUserId.isValid() || readerUserId.toLongLong() != op.userId))
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：读者仅可续借本人借阅记录"));

    // 3.5) 惰性清理（仅 staff 办理时触发）：该书"超时待取释放 + 队首顺延"，避免已超时的
    //      ready_to_pick 预约永久卡住续借。promoteNext 自带事务，须在续借事务开启前调用；
    //      尽力而为，失败（无排队/无副本/状态竞争）忽略，不影响续借主流程。
    if (op.isStaff()) {
        CopyService copies(m_db);
        ReservationService resv(m_db, &copies, m_eligibility);
        resv.promoteNext(op, bookId);
    }

    // 4) 五条规则逐条检查，全部不通过原因一次性返回（3004）
    QStringList reasons;
    const QDate today = QDate::currentDate();

    if (!active)
        reasons << QStringLiteral("该借阅记录已归还");
    if (active && dueAt.isValid() && dueAt < today)
        reasons << QStringLiteral("存在未处理逾期，请先归还");

    QString perr;
    const std::optional<int> maxRenew = readIntParam(m_db, QStringLiteral("max_renew_times"), &perr);
    if (!maxRenew)
        return Result::fail(ErrorCode::DbError, perr);
    if (renewCount >= *maxRenew)
        reasons << QStringLiteral("已达到最大续借次数（%1/%2）").arg(renewCount).arg(*maxRenew);

    // 该图书无"其他读者"的排队中/待取预约
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM reservations "
        "WHERE book_id = ? AND status IN ('waiting','ready_to_pick') AND reader_id <> ?"));
    q.addBindValue(bookId);
    q.addBindValue(readerId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("查询预约冲突失败: %1").arg(q.lastError().text()));
    if (q.value(0).toInt() > 0)
        reasons << QStringLiteral("该图书存在其他读者的有效预约，无法续借");

    // 读者账号正常（复用 B 资格判断，只消费账号类原因；借阅上限不参与续借判断）
    if (m_eligibility) {
        const EligibilityResult e = m_eligibility(readerId);
        if (e.accountProblem)
            reasons << QStringLiteral("读者账号状态异常");
    }

    if (!reasons.isEmpty())
        return failC(ErrorCodeC::RenewRejected, reasons.join(QStringLiteral("；")));

    // 5) 新应还日期 = 原应还日期 + 续借期限（参数）
    const std::optional<int> renewDays = readIntParam(m_db, QStringLiteral("renew_period_days"), &perr);
    if (!renewDays || *renewDays < 1)
        return Result::fail(ErrorCode::DbError,
                            perr.isEmpty() ? QStringLiteral("系统参数 renew_period_days 非法") : perr);
    const QDate newDue = dueAt.addDays(*renewDays);

    // 6) 事务更新
    if (!m_db.transaction())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("无法开启续借事务: %1").arg(m_db.lastError().text()));
    q.prepare(QStringLiteral("UPDATE loans SET due_at = ?, renew_count = renew_count + 1 "
                             "WHERE id = ? AND returned_at IS NULL"));
    q.addBindValue(newDue);
    q.addBindValue(loanId);
    if (!q.exec() || q.numRowsAffected() != 1) {
        m_db.rollback();
        return failC(ErrorCodeC::LoanNotFound, QStringLiteral("借阅记录已变化（可能已归还），续借已取消"));
    }
    if (!m_db.commit())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("续借事务提交失败: %1").arg(m_db.lastError().text()));

    Result r = Result::success(QStringLiteral("续借成功"));
    r.data.insert(QStringLiteral("loan_id"), loanId);
    r.data.insert(QStringLiteral("new_due_at"), newDue.toString(QStringLiteral("yyyy-MM-dd")));
    r.data.insert(QStringLiteral("renew_count"), renewCount + 1);
    return r;
}

} // namespace lib
