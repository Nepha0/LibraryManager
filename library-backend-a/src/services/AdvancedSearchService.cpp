#include "AdvancedSearchService.h"
#include "search_core.h"
#include <QSqlQuery>

namespace lib {

AdvancedSearchService::AdvancedSearchService(QSqlDatabase db) : m_db(db) {}

namespace {

// 把 QVariant 值转为字符串列表（兼容 QStringList / QVariantList / 单字符串）
QStringList toStrList(const QVariant& v)
{
    QStringList out;
    if (v.canConvert<QStringList>()) {
        const QStringList l = v.toStringList();
        for (const QString& s : l) out << s;
    } else if (v.canConvert<QVariantList>()) {
        const QVariantList l = v.toList();
        for (const QVariant& e : l) out << e.toString();
    } else {
        const QString s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    QStringList trimmed;
    for (const QString& s : out)
        if (!s.trimmed().isEmpty()) trimmed << s.trimmed();
    return trimmed;
}

QStringList orConditions(const QStringList& values,
                         const QStringList& columns,
                         QVariantList* binds, const QString& likeWrap = QString())
{
    // 同类别多词条 OR：columns 为该类别可匹配的列（如书名可同时匹配 title）
    QStringList parts;
    for (const QString& v : values) {
        QStringList cols;
        for (const QString& col : columns) {
            cols << QStringLiteral("%1 LIKE ?").arg(col);
            if (likeWrap.isEmpty())
                binds->append(QStringLiteral("%%1%").arg(v));
            else if (likeWrap == QStringLiteral("both"))
                binds->append(QStringLiteral("%%1%").arg(v));
            else
                binds->append(v);
        }
        parts << QStringLiteral("(") + cols.join(QStringLiteral(" OR ")) + QStringLiteral(")");
    }
    return parts;
}

} // namespace

Result AdvancedSearchService::advancedSearch(const OperatorContext& op,
                                             const QVariantMap& conditions,
                                             const QString& sortBy, bool ascending,
                                             int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    // ---------- 排序参数校验 ----------
    QString orderCol;
    if (sortBy == QStringLiteral("title"))
        orderCol = QStringLiteral("b.title COLLATE NOCASE");
    else if (sortBy == QStringLiteral("publish_date"))
        orderCol = QStringLiteral("b.publish_date");
    else if (sortBy == QStringLiteral("available"))
        orderCol = QStringLiteral("available_count");
    else
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("排序字段非法：%1（仅支持 title / publish_date / available）").arg(sortBy));
    const QString orderBy =
        QStringLiteral(" ORDER BY %1 %2").arg(orderCol, ascending ? QStringLiteral("ASC")
                                                                  : QStringLiteral("DESC"));

    // ---------- 条件解析 ----------
    QStringList where;
    QStringList having;
    QVariantList binds;
    QStringList invalidReasons;
    int validConditions = 0;

    // 书名（同类多词条 OR）
    {
        const QStringList titles = toStrList(conditions.value(QStringLiteral("titles")));
        if (!titles.isEmpty()) {
            where << QStringLiteral("(")
                  + orConditions(titles, { QStringLiteral("b.title") }, &binds)
                        .join(QStringLiteral(" OR "))
                  + QStringLiteral(")");
            ++validConditions;
        }
    }
    // 作者
    {
        const QStringList authors = toStrList(conditions.value(QStringLiteral("authors")));
        if (!authors.isEmpty()) {
            where << QStringLiteral("(")
                  + orConditions(authors, { QStringLiteral("b.author") }, &binds)
                        .join(QStringLiteral(" OR "))
                  + QStringLiteral(")");
            ++validConditions;
        }
    }
    // ISBN（精确）
    {
        const QString isbn = conditions.value(QStringLiteral("isbn")).toString().trimmed();
        if (!isbn.isEmpty()) {
            where << QStringLiteral("b.isbn = ?");
            binds << isbn;
            ++validConditions;
        }
    }
    // 分类多选（同类 OR）
    {
        QVariantList ids;
        const QVariant raw = conditions.value(QStringLiteral("categoryIds"));
        if (raw.canConvert<QVariantList>())
            ids = raw.toList();
        else if (raw.isValid() && !raw.isNull())
            ids << raw;
        QStringList parts;
        for (const QVariant& id : ids) {
            bool convOk = false;
            const qint64 cid = id.toLongLong(&convOk);
            if (convOk && cid > 0) {
                parts << QStringLiteral("b.category_id = ?");
                binds << cid;
            }
        }
        if (!parts.isEmpty()) {
            where << QStringLiteral("(") + parts.join(QStringLiteral(" OR ")) + QStringLiteral(")");
            ++validConditions;
        } else if (!ids.isEmpty()) {
            invalidReasons << QStringLiteral("分类条件非法：categoryIds 必须为正整数 id 列表");
        }
    }
    // 出版社（同类多词条 OR，模糊）
    {
        const QStringList pubs = toStrList(conditions.value(QStringLiteral("publishers")));
        if (!pubs.isEmpty()) {
            where << QStringLiteral("(")
                  + orConditions(pubs, { QStringLiteral("b.publisher") }, &binds)
                        .join(QStringLiteral(" OR "))
                  + QStringLiteral(")");
            ++validConditions;
        }
    }
    // 出版年区间
    {
        const QVariant yf = conditions.value(QStringLiteral("yearFrom"));
        const QVariant yt = conditions.value(QStringLiteral("yearTo"));
        const bool hasFrom = yf.isValid() && !yf.isNull() && yf.toInt() > 0;
        const bool hasTo   = yt.isValid() && !yt.isNull() && yt.toInt() > 0;
        if (hasFrom && hasTo && yf.toInt() > yt.toInt()) {
            invalidReasons << QStringLiteral("出版年区间非法：起始年(%1)大于结束年(%2)")
                                  .arg(yf.toInt()).arg(yt.toInt());
        } else if (hasFrom || hasTo) {
            const QString yearExpr = QStringLiteral("CAST(substr(b.publish_date, 1, 4) AS INTEGER)");
            if (hasFrom) {
                where << QStringLiteral("%1 >= ?").arg(yearExpr);
                binds << yf.toInt();
            }
            if (hasTo) {
                where << QStringLiteral("%1 <= ?").arg(yearExpr);
                binds << yt.toInt();
            }
            ++validConditions;
        }
    }
    // 馆藏状态：必须区分"可借 / 不可借"
    {
        const QString cs = conditions.value(QStringLiteral("copyStatus")).toString().trimmed().toUpper();
        if (!cs.isEmpty()) {
            if (cs == QStringLiteral("AVAILABLE")) {
                having << QStringLiteral("COALESCE(SUM(CASE WHEN cp.status = 'AVAILABLE' THEN 1 ELSE 0 END), 0) > 0");
                ++validConditions;
            } else if (cs == QStringLiteral("UNAVAILABLE")) {
                having << QStringLiteral("COALESCE(SUM(CASE WHEN cp.status = 'AVAILABLE' THEN 1 ELSE 0 END), 0) = 0");
                ++validConditions;
            } else {
                invalidReasons << QStringLiteral("馆藏状态非法：%1（仅支持 AVAILABLE / UNAVAILABLE）").arg(cs);
            }
        }
    }
    // 馆藏位置（同类多词条 OR，模糊）
    {
        const QStringList locs = toStrList(conditions.value(QStringLiteral("locations")));
        if (!locs.isEmpty()) {
            QStringList parts;
            for (const QString& loc : locs) {
                parts << QStringLiteral("EXISTS (SELECT 1 FROM copies lc"
                                        " WHERE lc.book_id = b.id AND lc.location LIKE ?)");
                binds << QStringLiteral("%%1%").arg(loc);
            }
            where << QStringLiteral("(") + parts.join(QStringLiteral(" OR ")) + QStringLiteral(")");
            ++validConditions;
        }
    }

    // ---------- 空条件 / 全非法条件处理 ----------
    if (validConditions == 0 && invalidReasons.isEmpty()) {
        Result r = Result::success();
        r.total = 0;
        r.message = QStringLiteral("检索条件为空，已返回空结果");
        return r;
    }
    if (validConditions == 0 && !invalidReasons.isEmpty())
        return Result::fail(ErrorCode::InvalidCondition,
                            QStringLiteral("检索条件全部非法：%1").arg(invalidReasons.join(QStringLiteral("；"))));

    const QString whereSql = where.isEmpty()
                                 ? QStringLiteral(" WHERE b.status = 'ACTIVE'")
                                 : QStringLiteral(" WHERE b.status = 'ACTIVE' AND ")
                                       + where.join(QStringLiteral(" AND "));
    // 个别非法条件被忽略时给出提示（其余条件仍然执行）
    QVariantList rows;
    int total = 0;
    Result r = SearchCore::queryBooks(m_db, whereSql, having, binds, orderBy,
                                      page, pageSize, &rows, &total);
    if (!r.ok()) return r;
    r.total = total;
    r.list = rows;
    if (!invalidReasons.isEmpty())
        r.message = QStringLiteral("以下非法条件已忽略：%1").arg(invalidReasons.join(QStringLiteral("；")));
    return r;
}

} // namespace lib
