#ifndef LIB_BORROW_SERVICE_H
#define LIB_BORROW_SERVICE_H
/*
 * BorrowService —— 图书借阅 + 借阅记录查询（P0）
 * 契约见 library-backend-c/docs/接口契约-C.md §2。
 *
 * borrow 事务步骤（每步失败返回具体错误码，任一步失败整体回滚）：
 *   1) OperatorContext 校验（1001/1002）
 *   2) B 板块读者资格判断（端口注入，3001）
 *   3) A 板块 getCopyByBarcode 副本校验（1011/3002/1013）
 *   4) 双保险：该副本已存在有效借阅 → 3002
 *   5) INSERT loans（borrowed_at/due_at 由 C++ QDate 计算，期限=borrow_period_days 参数）
 *   6) A 板块 changeCopyStatus(op, copyId, "borrowed")（并发失败 1007 → 回滚透出）
 *
 * 副本状态变更只能经 CopyService（A 端状态机 + 乐观锁），禁止直接 UPDATE book_copies。
 */
#include "../contract_c.h"

namespace lib {

class CopyService;

class BorrowService {
public:
    // copies 为 A 端 CopyService（同一条 QSqlDatabase 连接）；eligibility 为 B 端资格判断端口，可为空（仅测试）
    BorrowService(QSqlDatabase db, CopyService* copies, EligibilityFn eligibility);

    Result borrow(const OperatorContext& op, qint64 readerId, const QString& copyBarcode,
                  const LoanAuditFn& audit = {});
    // → data: { loan_id, reader_id, copy_id, book_id, borrowed_at, due_at }（日期 yyyy-MM-dd）

    Result getLoan(const OperatorContext& op, qint64 loanId);
    // → data: 单条借阅记录（行结构见 §3）

    // filter 均可省略：reader_id / book_id / barcode(模糊) / reader_no(模糊) / from / to("yyyy-MM-dd")
    //                / status: "active"(在借) | "returned"(已还) | "overdue"(在借且应还日期<今天)
    Result listLoans(const OperatorContext& op, const QVariantMap& filter,
                     int page = 1, int pageSize = 10);
    // → list 行: { loan_id, reader_id, reader_no, reader_name, book_id, title, barcode,
    //              borrowed_at, due_at, returned_at(""), overdue_days, status_text("在借"/"已还"/"逾期") }, total

private:
    QSqlDatabase m_db;
    CopyService* m_copies;
    EligibilityFn m_eligibility;
};

} // namespace lib

#endif // LIB_BORROW_SERVICE_H
