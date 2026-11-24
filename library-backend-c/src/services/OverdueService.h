#ifndef LIB_OVERDUE_SERVICE_H
#define LIB_OVERDUE_SERVICE_H
/*
 * OverdueService —— 逾期识别与展示（P1）
 * 契约见 library-backend-c/docs/接口契约-C.md §6。
 *
 * 实时判定（不做定时任务，查询时计算）：
 *   mode=current：returned_at IS NULL AND due_at < 今天（仅逾期在借）
 *   mode=all    ：追加已归还但归还日晚于应还日的历史记录
 * 无费用列、无结清操作。
 */
#include "../contract_c.h"

namespace lib {

class OverdueService {
public:
    explicit OverdueService(QSqlDatabase db);

    Result listOverdue(const OperatorContext& op, const QVariantMap& filter,
                       int page = 1, int pageSize = 10);
    // filter: mode("current"|"all"，缺省 current) / reader_id / book_id
    // → list 行: { loan_id, reader_id, reader_no, reader_name, book_id, title, barcode,
    //              due_at, overdue_days, status_text("在借-逾期"/"已归还") }, total

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_OVERDUE_SERVICE_H
