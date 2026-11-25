#ifndef LIB_RETURN_SERVICE_H
#define LIB_RETURN_SERVICE_H
/*
 * ReturnService —— 图书归还（P0）
 * 契约见 library-backend-c/docs/接口契约-C.md §3。
 *
 * returnBook 事务步骤（任一步失败整体回滚）：
 *   1) OperatorContext 校验（1001/1002）
 *   2) 定位在借记录（loan_id 或 copy_barcode 二选一）→ 3003
 *   3) UPDATE loans.returned_at = 今天；逾期天数 = 归还日 - 应还日（仅展示，无费用）
 *   4) 查该书 waiting 队首预约：
 *        有 → A: changeCopyStatus(copyId,"reserved") + 队首转 ready_to_pick + 写 pickup_deadline
 *        无 → A: changeCopyStatus(copyId,"available")
 *
 * 副本状态变更只能经 CopyService（A 端状态机 + 乐观锁），禁止直接 UPDATE book_copies。
 */
#include "../contract_c.h"

namespace lib {

class CopyService;

class ReturnService {
public:
    ReturnService(QSqlDatabase db, CopyService* copies);

    // locator: {"loan_id": qint64} 或 {"copy_barcode": QString}
    Result returnBook(const OperatorContext& op, const QVariantMap& locator,
                      const LoanAuditFn& audit = {});
    // → data: { loan_id, returned_at, overdue_days, reservation_triggered, copy_status("available"|"reserved") }

private:
    QSqlDatabase m_db;
    CopyService* m_copies;
};

} // namespace lib

#endif // LIB_RETURN_SERVICE_H
