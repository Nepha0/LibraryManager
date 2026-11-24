#ifndef LIB_PERSONAL_SERVICE_H
#define LIB_PERSONAL_SERVICE_H
/*
 * PersonalService —— 个人中心聚合（P1）
 * 契约见 library-backend-c/docs/接口契约-C.md §7。
 *
 * 越权强制（业务层，非仅前端隐藏）：
 *   op.role == Reader 时必须 readers.user_id == op.userId，否则 1002；
 *   不提供任何切换读者的查询参数。
 * data 分区：profile（读 B 侧表）/ active_loans / history / reservations / overdue。
 */
#include "../contract_c.h"

namespace lib {

class PersonalService {
public:
    explicit PersonalService(QSqlDatabase db);

    Result getPersonalOverview(const OperatorContext& op, qint64 readerId);
    // → data: {
    //     profile:      { reader_id, reader_no, name, phone, status, registered_at },
    //     active_loans: [ { loan_id, book_id, title, barcode, borrowed_at, due_at, overdue_days, renew_count } ],
    //     history:      [ 同上 + returned_at ],
    //     reservations: [ { reservation_id, book_id, title, queue_no, status, status_text, requested_at, pickup_deadline } ],
    //     overdue:      [ { loan_id, title, due_at, overdue_days } ]
    //   }

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_PERSONAL_SERVICE_H
