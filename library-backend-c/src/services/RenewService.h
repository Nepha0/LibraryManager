#ifndef LIB_RENEW_SERVICE_H
#define LIB_RENEW_SERVICE_H
/*
 * RenewService —— 图书续借（P1）
 * 契约见 library-backend-c/docs/接口契约-C.md §4。
 *
 * renew 规则（全部通过才可续借；任一不通过 → 3004，message 逐条列出全部原因）：
 *   1) 记录处于在借状态
 *   2) renew_count < max_renew_times（参数）
 *   3) 该图书无其他读者的排队中/待取预约
 *   4) 读者账号与读者状态正常（复用 B 资格判断的账号类原因；借阅上限不参与续借判断）
 *   5) 该记录无未处理逾期（在借且应还日期<今天）
 * 新应还日期 = 原应还日期 + renew_period_days（参数）；成功后 renew_count + 1。
 *
 * 权限：staff 可续借任意记录；Reader 仅可续借本人记录（readers.user_id == op.userId，否则 1002）。
 */
#include "../contract_c.h"

namespace lib {

class RenewService {
public:
    // eligibility 为 B 端资格判断端口（续借场景只消费账号类原因），可为空（仅测试）
    explicit RenewService(QSqlDatabase db, EligibilityFn eligibility = {});

    Result renew(const OperatorContext& op, qint64 loanId);
    // → data: { loan_id, new_due_at, renew_count }

private:
    QSqlDatabase m_db;
    EligibilityFn m_eligibility;
};

} // namespace lib

#endif // LIB_RENEW_SERVICE_H
