#ifndef LIB_COPY_SERVICE_H
#define LIB_COPY_SERVICE_H
/*
 * CopyService —— 馆藏副本管理 + 状态机（P0）
 * 完成标准对应 2.2「馆藏副本与状态机」：
 *  - addCopy / updateCopy / changeCopyStatus / listCopiesByBook
 *  - 状态机为唯一合法转换源（写在 Service 层，静态转换表）
 *  - 维修中/遗失报损的恢复与终结仅管理员可操作
 *  - 非法转换拒绝并返回当前状态；条码重复拒绝
 *
 * 状态机（A 牵头定义，C 板块借阅/归还/预约流程调用 changeCopyStatus 驱动）：
 *   在馆可借 AVAILABLE → 借出 / 预约保留待取 / 维修中 / 遗失报损
 *   借出 BORROWED      → 在馆可借（归还）/ 预约保留待取（归还且有预约）
 *   预约保留待取 RESERVED → 借出（预约者取书）/ 在馆可借（超时释放或取消）
 *   维修中 MAINTENANCE / 遗失报损 LOST → 仅管理员可恢复(→AVAILABLE)或终结(→LOST)
 */
#include "../common/contract.h"
#include <QSqlDatabase>

namespace lib {

class CopyService {
public:
    explicit CopyService(QSqlDatabase db);

    // 新增副本（状态置为在馆可借）。barcode 必填且全局唯一
    Result addCopy(const OperatorContext& op, qint64 bookId, const QString& barcode,
                   const QString& location = QString(),
                   const QDate& acquiredDate = QDate());

    // 编辑副本基本信息（仅 location / acquired_date；状态必须走 changeCopyStatus）
    Result updateCopy(const OperatorContext& op, qint64 copyId, const QVariantMap& fields);

    // 状态变更（唯一合法入口）
    Result changeCopyStatus(const OperatorContext& op, qint64 copyId, const QString& newStatus);

    // 按图书查副本（分页）；data 携带该书副本统计
    Result listCopiesByBook(const OperatorContext& op, qint64 bookId,
                            int page = 1, int pageSize = 10);

    // 通用副本查询。filter: book_id / status / location(模糊) / barcode(模糊)
    Result listCopies(const OperatorContext& op, const QVariantMap& filter,
                      int page = 1, int pageSize = 10);

    // 按条码定位副本 —— 供 C 板块借阅/归还流程使用
    Result getCopyByBarcode(const OperatorContext& op, const QString& barcode);

    // 按图书副本统计（各状态数量行，供详情弹窗）
    Result copyStatistics(const OperatorContext& op, qint64 bookId);

    // 状态机转换表（公开给测试与 C 板块核对）
    static bool isTransitionLegal(CopyStatus from, CopyStatus to);

private:
    Result fetchCopyRow(qint64 copyId, QVariantMap* out);
    Result fetchCopyByBarcode(const QString& barcode, QVariantMap* out);

    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_COPY_SERVICE_H
