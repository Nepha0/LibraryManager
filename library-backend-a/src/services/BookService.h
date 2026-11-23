#ifndef LIB_BOOK_SERVICE_H
#define LIB_BOOK_SERVICE_H
/*
 * BookService —— 图书信息 CRUD / 下架 / 删除（P0）
 * 完成标准对应 2.2「图书信息 CRUD」：
 *  - addBook / updateBook / getBook(含副本统计) / listBooks(分页筛选) / shelveOff(下架)
 *  - ISBN 唯一校验（业务层，ACTIVE 范围内唯一；下架书视为逻辑删除，允许重新录入同 ISBN）
 *  - 物理删除接口对有借阅记录的图书返回拒绝并提示改用下架（逻辑删除）
 */
#include "../common/contract.h"
#include <QSqlDatabase>

namespace lib {

class BookService {
public:
    explicit BookService(QSqlDatabase db);

    // 新增图书。fields 必填：isbn、title、category_id；可选：author、publisher、
    // publish_date(yyyy-MM-dd)、intro、cover_path
    Result addBook(const OperatorContext& op, const QVariantMap& fields);

    // 更新图书。fields 中出现的键才更新（白名单：title/author/publisher/publish_date/
    // category_id/intro/cover_path/isbn）
    Result updateBook(const OperatorContext& op, qint64 bookId, const QVariantMap& fields);

    // 图书详情，含副本各状态统计（data: book字段 + copy_total/copy_available/.../locations）
    Result getBook(const OperatorContext& op, qint64 bookId);

    // 分页筛选。filter: keyword(书名/作者/ISBN 模糊)、category_id、publisher(模糊)、
    // status(ACTIVE/SHELVED_OFF，缺省全部)
    Result listBooks(const OperatorContext& op, const QVariantMap& filter,
                     int page = 1, int pageSize = 10);

    // 下架（逻辑删除）。下架后图书不可再借出（CopyService 有防御校验）
    Result shelveOff(const OperatorContext& op, qint64 bookId);

    // 物理删除。存在借阅记录时拒绝（HasBorrowRecords），提示改用下架
    Result deleteBook(const OperatorContext& op, qint64 bookId);

private:
    Result fetchBookRow(qint64 bookId, QVariantMap* out);
    Result validateFields(const QVariantMap& fields, bool forCreate, qint64 excludeBookId,
                          QVariantMap* normalized);

    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_BOOK_SERVICE_H
