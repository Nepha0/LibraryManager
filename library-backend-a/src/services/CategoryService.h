#ifndef LIB_CATEGORY_SERVICE_H
#define LIB_CATEGORY_SERVICE_H
/*
 * CategoryService —— 分类管理（P1）
 * 完成标准对应 1.3.6：
 *  - 列表（含图书引用计数）+ 新增 / 编辑 / 删除
 *  - 删除被图书引用的分类时返回限制提示（CategoryInUse）
 */
#include "common/contract.h"
#include <QSqlDatabase>

namespace lib {

class CategoryService {
public:
    explicit CategoryService(QSqlDatabase db);

    // 新增分类。fields 必填：name；可选：description
    Result addCategory(const OperatorContext& op, const QVariantMap& fields);

    // 编辑分类（name / description）
    Result updateCategory(const OperatorContext& op, qint64 categoryId, const QVariantMap& fields);

    // 删除分类；被图书引用时拒绝并返回引用数量
    Result deleteCategory(const OperatorContext& op, qint64 categoryId);

    // 分类列表（keyword 名称模糊筛选；含 book_count 引用计数）；分页
    Result listCategories(const OperatorContext& op, const QString& keyword = QString(),
                          int page = 1, int pageSize = 50);

    Result getCategory(const OperatorContext& op, qint64 categoryId);

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_CATEGORY_SERVICE_H
