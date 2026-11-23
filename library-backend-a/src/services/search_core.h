#ifndef LIB_SEARCH_CORE_H
#define LIB_SEARCH_CORE_H
/*
 * search_core.h —— SearchService / AdvancedSearchService 共用的图书行构造器。
 * 每行附带：可借数量、馆藏总数、馆藏位置（去重）。
 */
#include "../common/contract.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

namespace lib {

struct SearchCore {
    // 通用分组查询。whereSql 形如 " WHERE ..."（可为空）；havingSql 为 HAVING 片段列表；
    // orderBySql 形如 " ORDER BY ..."（可为空，默认按 b.title 升序）。
    static Result queryBooks(QSqlDatabase db,
                             const QString& whereSql,
                             const QStringList& havingSql,
                             const QVariantList& binds,
                             const QString& orderBySql,
                             int page, int pageSize,
                             QVariantList* rows, int* total)
    {
        const QString select =
            QStringLiteral(
                "SELECT b.id, b.isbn, b.title, b.author, b.publisher, b.publish_date, b.status,"
                " c.name AS category_name,"
                " COUNT(cp.id) AS copy_total,"
                " COALESCE(SUM(CASE WHEN cp.status = 'AVAILABLE' THEN 1 ELSE 0 END), 0) AS available_count,"
                " GROUP_CONCAT(DISTINCT cp.location) AS locations"
                " FROM books b"
                " LEFT JOIN categories c ON c.id = b.category_id"
                " LEFT JOIN copies cp ON cp.book_id = b.id");
        const QString groupBy = QStringLiteral(" GROUP BY b.id");
        const QString having =
            havingSql.isEmpty() ? QString()
                                : QStringLiteral(" HAVING ") + havingSql.join(QStringLiteral(" AND "));

        // 1) 总数（分组后计数）
        QSqlQuery q(db);
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM (%1%2%3%4)")
                      .arg(select, whereSql, groupBy, having));
        for (const QVariant& b : binds) q.addBindValue(b);
        if (!q.exec() || !q.next())
            return Result::fail(ErrorCode::DbError,
                                QStringLiteral("统计检索总数失败: %1").arg(q.lastError().text()));
        *total = q.value(0).toInt();

        // 2) 分页数据
        const QString order = orderBySql.isEmpty()
                                  ? QStringLiteral(" ORDER BY b.title COLLATE NOCASE ASC")
                                  : orderBySql;
        q.prepare(QStringLiteral("%1%2%3%4%5 LIMIT %6 OFFSET %7")
                      .arg(select, whereSql, groupBy, having, order)
                      .arg(pageSize)
                      .arg((page - 1) * pageSize));
        for (const QVariant& b : binds) q.addBindValue(b);
        if (!q.exec())
            return Result::fail(ErrorCode::DbError,
                                QStringLiteral("检索查询失败: %1").arg(q.lastError().text()));

        *rows = QVariantList();
        while (q.next()) {
            const int availableCount = q.value(QStringLiteral("available_count")).toInt();
            QVariantMap row;
            row.insert(QStringLiteral("book_id"),      q.value(QStringLiteral("id")));
            row.insert(QStringLiteral("isbn"),         q.value(QStringLiteral("isbn")));
            row.insert(QStringLiteral("title"),        q.value(QStringLiteral("title")));
            row.insert(QStringLiteral("author"),       q.value(QStringLiteral("author")));
            row.insert(QStringLiteral("publisher"),    q.value(QStringLiteral("publisher")));
            row.insert(QStringLiteral("publish_date"), q.value(QStringLiteral("publish_date")));
            row.insert(QStringLiteral("category_name"),q.value(QStringLiteral("category_name")));
            row.insert(QStringLiteral("copy_total"),   q.value(QStringLiteral("copy_total")).toInt());
            row.insert(QStringLiteral("available_count"), availableCount);
            // 馆藏状态 Badge：必须区分"可借/不可借"，不允许只显示"有馆藏"
            row.insert(QStringLiteral("borrowable"),   availableCount > 0);
            row.insert(QStringLiteral("status_text"),
                       availableCount > 0 ? QStringLiteral("可借") : QStringLiteral("不可借"));
            // 馆藏位置（去重、去空）
            QStringList locations;
            const QString locRaw = q.value(QStringLiteral("locations")).toString();
            if (!locRaw.isEmpty()) {
                for (const QString& s : locRaw.split(QLatin1Char(','), Qt::SkipEmptyParts))
                    if (!s.trimmed().isEmpty()) locations << s.trimmed();
            }
            row.insert(QStringLiteral("locations"), locations);
            row.insert(QStringLiteral("locations_text"),
                       locations.isEmpty() ? QString()
                                           : locations.join(QStringLiteral("、")));
            *rows << row;
        }
        return Result::success();
    }
};

} // namespace lib

#endif // LIB_SEARCH_CORE_H
