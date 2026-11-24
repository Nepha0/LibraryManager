#ifndef LIB_RESERVATION_SERVICE_H
#define LIB_RESERVATION_SERVICE_H
/*
 * ReservationService —— 图书预约（P1）
 * 契约见 library-backend-c/docs/接口契约-C.md §5。
 *
 * 预约状态机（唯一合法转换源：ReservationService，非法流转 → 3008）：
 *   waiting       → ready_to_pick（归还触发/手工处理）| cancelled
 *   ready_to_pick → finished（待取借出完成）| cancelled（取消，须先释放副本）
 *   finished / cancelled / expired → 终态
 *
 * 规则要点：
 *   - 仅当该书 copy_available == 0 时可提交（A: copyStatistics）→ 否则 3006
 *   - 同一读者同书不可重复排队（业务校验 → 3005；uk_active_reservation 唯一约束兜底）
 *   - queue_no = 该书当前最大排队号 + 1
 *   - 超时失效惰性触发：每次队列操作前先释放 pickup_deadline < 现在的 ready_to_pick
 *     （副本经 A: changeCopyStatus → "available"），再顺延下一位
 *   - 副本状态变更只能经 CopyService，禁止直接 UPDATE book_copies
 */
#include "../contract_c.h"

namespace lib {

class CopyService;

class ReservationService {
public:
    // copies 为 A 端 CopyService（同一条 QSqlDatabase 连接）；eligibility 为 B 端资格判断端口，可为空
    explicit ReservationService(QSqlDatabase db, CopyService* copies, EligibilityFn eligibility = {});

    Result reserve(const OperatorContext& op, qint64 readerId, qint64 bookId);
    // → data: { reservation_id, queue_no, requested_at }

    Result cancelReservation(const OperatorContext& op, qint64 reservationId);
    // waiting→cancelled 直接取消；ready_to_pick→cancelled 先释放副本（A → "available"）

    // "处理"：队首 waiting→ready_to_pick，从该书 available 副本中选一本
    // 经 A: changeCopyStatus → "reserved"，写 pickup_deadline；无排队 → 3009；无可用副本 → 3002
    Result promoteNext(const OperatorContext& op, qint64 bookId);

    Result finishReservation(const OperatorContext& op, qint64 reservationId);
    // ready_to_pick→finished（预约者取书借阅成功时由 borrow 流程自动完成，亦可手工确认）

    Result listReservationQueue(const OperatorContext& op, qint64 bookId,
                                int page = 1, int pageSize = 10);
    // → list 行: { reservation_id, reader_id, reader_no, reader_name, queue_no, status,
    //              status_text, requested_at, pickup_deadline }, total

    // filter: reader_id / book_id / status(waiting|ready_to_pick|finished|cancelled|expired)
    Result listReservations(const OperatorContext& op, const QVariantMap& filter,
                            int page = 1, int pageSize = 10);

private:
    // 惰性释放该书已超时的待取预约（须在事务内调用）：expired + 副本经 A → available
    Result releaseExpiredReadyToPick(const OperatorContext& op, qint64 bookId);

    QSqlDatabase m_db;
    CopyService* m_copies;
    EligibilityFn m_eligibility;
};

} // namespace lib

#endif // LIB_RESERVATION_SERVICE_H
