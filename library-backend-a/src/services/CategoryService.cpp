#include "CategoryService.h"
#include <QSqlQuery>
#include <QSqlError>

namespace lib {

CategoryService::CategoryService(QSqlDatabase db) : m_db(db) {}

Result CategoryService::addCategory(const OperatorContext& op, const QVariantMap& fields)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可新增分类"));

    const QString name = fields.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty())
        return Result::fail(ErrorCode::MissingField, QStringLiteral("必填字段缺失：分类名称"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM categories WHERE name = ?"));
    q.addBindValue(name);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("分类名查重失败"));
    if (q.value(0).toInt() > 0)
        return Result::fail(ErrorCode::NameDuplicate,
                            QStringLiteral("分类「%1」已存在").arg(name));

    q.prepare(QStringLiteral("INSERT INTO categories (name, description) VALUES (?, ?)"));
    q.addBindValue(name);
    q.addBindValue(fields.value(QStringLiteral("description")).toString().trimmed());
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("新增分类失败: %1").arg(q.lastError().text()));
    return getCategory(op, q.lastInsertId().toLongLong());
}

Result CategoryService::updateCategory(const OperatorContext& op, qint64 categoryId,
                                       const QVariantMap& fields)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isStaff())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员/图书管理员可编辑分类"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM categories WHERE id = ?"));
    q.addBindValue(categoryId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::CategoryNotFound,
                            QStringLiteral("分类不存在（id=%1）").arg(categoryId));

    QStringList sets;
    QVariantList binds;
    if (fields.contains(QStringLiteral("name"))) {
        const QString name = fields.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty())
            return Result::fail(ErrorCode::MissingField, QStringLiteral("分类名称不能为空"));
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM categories WHERE name = ? AND id <> ?"));
        q.addBindValue(name);
        q.addBindValue(categoryId);
        if (!q.exec() || !q.next())
            return Result::fail(ErrorCode::DbError, QStringLiteral("分类名查重失败"));
        if (q.value(0).toInt() > 0)
            return Result::fail(ErrorCode::NameDuplicate,
                                QStringLiteral("分类「%1」已存在").arg(name));
        sets << QStringLiteral("name = ?");
        binds << name;
    }
    if (fields.contains(QStringLiteral("description"))) {
        sets << QStringLiteral("description = ?");
        binds << fields.value(QStringLiteral("description")).toString().trimmed();
    }
    if (sets.isEmpty())
        return Result::fail(ErrorCode::InvalidCondition, QStringLiteral("未提供任何可更新的字段"));

    q.prepare(QStringLiteral("UPDATE categories SET %1 WHERE id = %2")
                  .arg(sets.join(QStringLiteral(", "))).arg(categoryId));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("更新分类失败: %1").arg(q.lastError().text()));
    return getCategory(op, categoryId);
}

Result CategoryService::deleteCategory(const OperatorContext& op, qint64 categoryId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录，禁止操作"));
    if (!op.isAdmin())
        return Result::fail(ErrorCode::NoPermission, QStringLiteral("无权限：仅管理员可删除分类"));

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name FROM categories WHERE id = ?"));
    q.addBindValue(categoryId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::CategoryNotFound,
                            QStringLiteral("分类不存在（id=%1）").arg(categoryId));
    const QString name = q.value(1).toString();

    // 被图书引用时拒绝
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM books WHERE category_id = ?"));
    q.addBindValue(categoryId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("引用计数查询失败"));
    const int refCount = q.value(0).toInt();
    if (refCount > 0)
        return Result::fail(ErrorCode::CategoryInUse,
                            QStringLiteral("该分类下存在 %1 本图书，无法删除，请先迁移或下架相关图书").arg(refCount));

    q.prepare(QStringLiteral("DELETE FROM categories WHERE id = ?"));
    q.addBindValue(categoryId);
    if (!q.exec() || q.numRowsAffected() != 1)
        return Result::fail(ErrorCode::DbError, QStringLiteral("删除分类失败"));
    return Result::success(QStringLiteral("分类「%1」已删除").arg(name));
}

Result CategoryService::listCategories(const OperatorContext& op, const QString& keyword,
                                       int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    QString whereSql;
    QVariantList binds;
    const QString kw = keyword.trimmed();
    if (!kw.isEmpty()) {
        whereSql = QStringLiteral(" WHERE c.name LIKE ?");
        binds << QStringLiteral("%%1%").arg(kw);
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM categories c%1").arg(whereSql));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::DbError, QStringLiteral("统计分类总数失败"));
    Result r;
    r.total = q.value(0).toInt();

    q.prepare(QStringLiteral(
        "SELECT c.id, c.name, c.description,"
        " (SELECT COUNT(*) FROM books b WHERE b.category_id = c.id) AS book_count"
        " FROM categories c%1 ORDER BY c.name LIMIT %2 OFFSET %3")
        .arg(whereSql).arg(pageSize).arg((page - 1) * pageSize));
    for (const QVariant& b : binds) q.addBindValue(b);
    if (!q.exec())
        return Result::fail(ErrorCode::DbError,
                            QStringLiteral("查询分类列表失败: %1").arg(q.lastError().text()));
    while (q.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("category_id"),  q.value(QStringLiteral("id")));
        row.insert(QStringLiteral("name"),         q.value(QStringLiteral("name")));
        row.insert(QStringLiteral("description"),  q.value(QStringLiteral("description")));
        row.insert(QStringLiteral("book_count"),   q.value(QStringLiteral("book_count")).toInt());
        r.list << row;
    }
    return r;
}

Result CategoryService::getCategory(const OperatorContext& op, qint64 categoryId)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT c.id, c.name, c.description,"
        " (SELECT COUNT(*) FROM books b WHERE b.category_id = c.id) AS book_count"
        " FROM categories c WHERE c.id = ?"));
    q.addBindValue(categoryId);
    if (!q.exec() || !q.next())
        return Result::fail(ErrorCode::CategoryNotFound,
                            QStringLiteral("分类不存在（id=%1）").arg(categoryId));
    Result r = Result::success();
    r.data.insert(QStringLiteral("category_id"), q.value(QStringLiteral("id")));
    r.data.insert(QStringLiteral("name"),        q.value(QStringLiteral("name")));
    r.data.insert(QStringLiteral("description"), q.value(QStringLiteral("description")));
    r.data.insert(QStringLiteral("book_count"),  q.value(QStringLiteral("book_count")).toInt());
    return r;
}

} // namespace lib
