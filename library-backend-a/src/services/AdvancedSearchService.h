#ifndef LIB_ADVANCED_SEARCH_SERVICE_H
#define LIB_ADVANCED_SEARCH_SERVICE_H
/*
 * AdvancedSearchService —— 多条件高级检索（P1）
 * 完成标准对应 2.2「多条件高级检索」：
 *  - advancedSearch(conditions, 排序, 分页)
 *  - 条件类别：书名[] / 作者[] / ISBN / 分类[] / 出版社 / 出版年区间 / 馆藏状态 / 馆藏位置
 *  - 类别间 AND，同类别多词条 OR
 *  - 排序支持：书名(title) / 出版时间(publish_date) / 可借数(available)
 *  - 空条件返回空结果而非全量；条件全部非法返回明确提示
 */
#include "../common/contract.h"
#include <QSqlDatabase>

namespace lib {

class AdvancedSearchService {
public:
    explicit AdvancedSearchService(QSqlDatabase db);

    /*
     * conditions（QVariantMap，键均可省略）：
     *   titles       : QStringList/QVariantList  书名词条（同类 OR）
     *   authors      : QStringList/QVariantList  作者词条（同类 OR）
     *   isbn         : QString                   ISBN（精确匹配）
     *   categoryIds  : QVariantList<int>         分类 id 多选（同类 OR）
     *   publishers   : QStringList/QVariantList  出版社词条（同类 OR，模糊）
     *   yearFrom/yearTo : int                    出版年区间（含边界）
     *   copyStatus   : QString                   "AVAILABLE"(可借) / "UNAVAILABLE"(不可借)
     *   locations    : QStringList/QVariantList  馆藏位置词条（同类 OR，模糊）
     * sortBy: "title" / "publish_date" / "available"；ascending 升降序
     */
    Result advancedSearch(const OperatorContext& op, const QVariantMap& conditions,
                          const QString& sortBy = QStringLiteral("title"),
                          bool ascending = true,
                          int page = 1, int pageSize = 10);

private:
    QSqlDatabase m_db;
};

} // namespace lib

#endif // LIB_ADVANCED_SEARCH_SERVICE_H
