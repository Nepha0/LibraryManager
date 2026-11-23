#ifndef LIB_IMPORT_EXPORT_SERVICE_H
#define LIB_IMPORT_EXPORT_SERVICE_H
/*
 * ImportExportService —— 数据导入 / 导出（P2）
 * 完成标准对应 2.2「数据导入/导出」：
 *  - 支持 CSV 与 .xlsx（xlsx 经内置 QXlsx 第三方库，无外部依赖）
 *  - 导入逐行校验，错误行汇总返回（行号 + 原因），合法行导入
 *  - 导出含图书与副本数据；导出按批次分页查询，支持大文件
 *
 * 图书导入列（表头顺序任意，必需列：ISBN、书名、分类）：
 *   ISBN, 书名, 作者, 出版社, 出版日期, 分类, 简介, 封面路径
 * 副本导入列（必需列：条码、ISBN）：
 *   条码, ISBN, 馆藏位置, 入藏日期
 */
#include "../common/contract.h"
#include <QSqlDatabase>
#include <QVector>
#include <QStringList>

namespace lib {

class ImportExportService {
public:
    explicit ImportExportService(QSqlDatabase db);

    // 导入图书（.csv / .xlsx）。返回 data: total_rows/imported_rows/error_rows
    Result importBooks(const OperatorContext& op, const QString& filePath);
    // 导入副本（.csv / .xlsx）
    Result importCopies(const OperatorContext& op, const QString& filePath);

    // 导出图书（含馆藏总数/可借数量/状态）
    Result exportBooks(const OperatorContext& op, const QString& filePath);
    // 导出副本（含所属图书 ISBN 与书名）
    Result exportCopies(const OperatorContext& op, const QString& filePath);

private:
    // CSV 逐行校验并入库的公共实现（bookMode=true 图书 / false 副本）
    Result importCsv(const OperatorContext& op, const QString& filePath, bool bookMode);
    Result importXlsx(const OperatorContext& op, const QString& filePath, bool bookMode);
    // 表头 → 列序号映射；返回错误信息（空串表示成功）
    static QString mapHeader(const QStringList& header, bool bookMode,
                             QHash<QString, int>* colIndex);

    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_IMPORT_EXPORT_SERVICE_H
