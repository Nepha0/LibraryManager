#include "SearchService.h"
#include "search_core.h"

namespace lib {

SearchService::SearchService(QSqlDatabase db) : m_db(db) {}

Result SearchService::search(const OperatorContext& op, const QString& keyword,
                             int page, int pageSize)
{
    if (!op.authenticated)
        return Result::fail(ErrorCode::NotAuthenticated, QStringLiteral("未登录"));
    if (!pageParamsValid(page, pageSize))
        return Result::fail(ErrorCode::InvalidPage,
                            QStringLiteral("分页参数非法：page=%1, pageSize=%2").arg(page).arg(pageSize));

    // 空条件返回空结果而非全量
    const QString kw = keyword.trimmed();
    if (kw.isEmpty()) {
        Result r = Result::success();
        r.total = 0;
        r.message = QStringLiteral("检索关键词为空，已返回空结果");
        return r;
    }

    const QString like = QStringLiteral("%%1%").arg(kw);
    const QString where = QStringLiteral(
        " WHERE b.status = 'ACTIVE'"
        " AND (b.title LIKE ? OR b.author LIKE ? OR b.isbn LIKE ?)");
    QVariantList binds { like, like, like };

    QVariantList rows;
    int total = 0;
    Result r = SearchCore::queryBooks(m_db, where, {}, binds, QString(),
                                      page, pageSize, &rows, &total);
    if (!r.ok()) return r;
    r.total = total;
    r.list = rows;
    return r;
}

} // namespace lib
